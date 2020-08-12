/*
 *
 *  compatible UDF implementation of
 *  IS_UUID() UUID_TO_BIN() BIN_TO_UUID()
 *  for MariaDB and older versions of MySQL
 *
 *  functions are documented at:
 *    https://dev.mysql.com/doc/refman/8.0/en/miscellaneous-functions.html
 *
 *  further reading:
 *    https://mariadb.com/kb/en/library/guiduuid-performance/
 *    https://www.percona.com/blog/2014/12/19/store-uuid-optimized-way/
 *
 *  installation:
 *    install uuid2bin.so ${plugin_dir}/
 *    CREATE FUNCTION is_uuid RETURNS INTEGER SONAME "uuid2bin.so";
 *    CREATE FUNCTION uuid_to_bin RETURNS STRING SONAME 'uuid2bin.so';
 *    CREATE FUNCTION bin_to_uuid RETURNS STRING SONAME 'uuid2bin.so';
 *
 *  uninstall:
 *    DROP FUNCTION is_uuid;
 *    DROP FUNCTION uuid_to_bin;
 *    DROP FUNCTION bin_to_uuid;
 *    rm ${plugin_dir}/uuid2bin.so
 *
 */

#include <stdint.h>
#include <string.h>
#include <string>
#include <mysql/mysql.h>
#include <mysql/my_global.h>

#ifdef HAVE_DLOPEN

#define UNUSED(expr) do { (void)(expr); } while (0)

extern "C" {

	// IS_UUID(string_uuid)
	my_bool is_uuid_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
	void is_uuid_deinit(UDF_INIT* initid);
	long long is_uuid(UDF_INIT *initid, UDF_ARGS* args, char* is_null, char* is_error);

	// UUID_TO_BIN(string_uuid), UUID_TO_BIN(string_uuid, swap_flag)
	my_bool uuid_to_bin_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
	void uuid_to_bin_deinit(UDF_INIT* initid);
	const char* uuid_to_bin(UDF_INIT *initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* is_error);

	// BIN_TO_UUID(binary_uuid), BIN_TO_UUID(binary_uuid, swap_flag)
	my_bool bin_to_uuid_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
	void bin_to_uuid_deinit(UDF_INIT* initid);
	const char* bin_to_uuid(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* is_error);

}

/*
 * UUID processing functions
 */

namespace {

inline int hexchar_to_int(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';

	c |= 32; // convert to lowercase

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	return -1;
}

/* accepts the formats accepted by MySQL's IS_UUID() */

bool uuid_unhexlify(const char *str, int length, char uuid[16])
{
	int i, upper, lower;
	const char *ptr;

	// 32-hexchar, no dashes
	if (length == 32) {
		ptr = str;
		for (i = 0; i < 16; i++) {
			upper = hexchar_to_int(ptr[0]);
			lower = hexchar_to_int(ptr[1]);
			if (upper < 0 || lower < 0)
				return false;
			uuid[i] = upper << 4 | lower;
			ptr += 2;
		}
		return true;
	}

	// wrapped by braces?
	if (length == 38 && str[0] == '{' && str[37] == '}') {
		// reposition pointer and fall through to 36-char dashed format
		str++;
		length -= 2;
	}

	// 36-hexchar, dashed
	if (length == 36) {
		ptr = str;
		for (i = 0; i < 16; i++) {

			// read next hexchar pair
			upper = hexchar_to_int(ptr[0]);
			lower = hexchar_to_int(ptr[1]);
			if (upper < 0 || lower < 0)
				return false;
			uuid[i] = upper << 4 | lower;
			ptr += 2;

			// handle dashes in the correct locations
			switch (i) {
				case 3:
				case 5:
				case 7:
				case 9:
					if (*ptr != '-')
						return false;
					ptr++;
			}

		}
		return true;
	}

	// anything else is invalid
	return false;
}

void uuid_hexlify(const char uuid[16], char out[36])
{
	static char hexdigits[] = "0123456789abcdef";
	int i, j;

	for (i = 0, j = 0; i < 16; i++) {
		out[j++] = hexdigits[(uuid[i] >> 4) & 0x0f];
		out[j++] = hexdigits[uuid[i] & 0x0f];
		switch (i) {
			case 3:
			case 5:
			case 7:
			case 9:
				out[j++] = '-';
				;;
		}
	}
}

// 4:time_low,2:time_mid,2:time_hi_and_version -> 2:time_hi_and_version,2:time_mid,4:time_low
inline void swap_ts(char uuid[16])
{
	char buf[4];

	memcpy(buf,    uuid,   4); // time_low,time_mid,time_hi_and_version + time_low
	memcpy(uuid,   uuid+6, 2); // time_hi_and_version,xxxxx,time_mid,time_hi_and_version + time_low
	memcpy(uuid+2, uuid+4, 2); // time_hi_and_version,time_mid,time_mid,time_hi_and_version + time_low
	memcpy(uuid+4, buf,    4); // time_hi_and_version,time_mid,time_low + time_low
}

// 4:time_hi_and_version,2:time_mid,2:time_low -> 2:time_low,2:time_mid,4:time_hi_and_version
inline void unswap_ts(char uuid[16])
{
	char buf[4];

	memcpy(buf,    uuid+4, 4); // time_hi_and_version,time_mid,time_low + time_low
	memcpy(uuid+4, uuid+2, 2); // time_hi_and_version,time_mid,time_mid,xxxxx + time_low
	memcpy(uuid+6, uuid,   2); // time_hi_and_version,time_mid,time_mid,time_hi_and_version + time_low
	memcpy(uuid,   buf,    4); // time_low,time_mid,time_hi_and_version + time_low
}

}

/*
 * IS_UUID(string)
 */

my_bool is_uuid_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
	if (args->arg_count != 1) {
		strcpy(message, "IS_UUID requires one argument");
		return 1;
	}

	initid->const_item = true;
	return 0;
}

void is_uuid_deinit(UDF_INIT* initid)
{
	UNUSED(initid);
}

long long is_uuid(UDF_INIT* initid, UDF_ARGS* args, char* is_null, char* is_error)
{
	char data[16];
	bool ok;

	UNUSED(initid);
	UNUSED(is_error);

	if (args->args[0] == NULL) {
		*is_null = 1;
		return 0;
	}

	if (args->arg_type[0] != STRING_RESULT) {
		return 0;
	}

	ok = uuid_unhexlify(args->args[0], args->lengths[0], data);

	return ok ? 1 : 0;
}

/*
 * UUID_TO_BIN(string)
 * UUID_TO_BIN(string, swap_flag)
 */

my_bool uuid_to_bin_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
	if (args->arg_count < 1 || args->arg_count > 2) {
		strcpy(message, "UUID_TO_BIN requires either one or two arguments");
		return 1;
	}

	if (args->arg_type[0] != STRING_RESULT) {
		strcpy(message, "UUID_TO_BIN requires first argument as string");
		return 1;
	}

	if (args->arg_count == 2 && args->arg_type[1] != INT_RESULT) {
		strcpy(message, "UUID_TO_BIN requires second argument as integer");
		return 1;
	}

	initid->const_item = true;
	initid->maybe_null = 1;
	initid->max_length = 16;
	return 0;
}

void uuid_to_bin_deinit(UDF_INIT* initid)
{
	UNUSED(initid);
}

const char* uuid_to_bin(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* is_error)
{
	UNUSED(initid);

	if (args->args[0] == NULL) {
		*is_null = 1;
		return NULL;
	}

	bool swap_flag = args->arg_count == 2 ? *reinterpret_cast<long long*>(args->args[1]) : false;

	char data[16];
	bool ok;

	ok = uuid_unhexlify(args->args[0], args->lengths[0], data);
	if (!ok) {
		*is_error = 1;
		return NULL;
	}

	if (swap_flag)
		swap_ts(data);

	memcpy(result, data, 16);
	*length = 16;
	return result;
}

/*
 * BIN_TO_UUID(string)
 * BIN_TO_UUID(string, swap_flag)
 */

my_bool bin_to_uuid_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
	if (args->arg_count < 1 || args->arg_count > 2) {
		strcpy(message, "BIN_TO_UUID requires either one or two arguments");
		return 1;
	}

	if (args->arg_type[0] != STRING_RESULT) {
		strcpy(message, "BIN_TO_UUID requires first argument as binary");
		return 1;
	}

	if (args->arg_count == 2 && args->arg_type[1] != INT_RESULT) {
		strcpy(message, "BIN_TO_UUID requires second argument as integer");
		return 1;
	}

	initid->const_item = false;
	initid->maybe_null = 1;
	initid->max_length = 36;
	return 0;
}

void bin_to_uuid_deinit(UDF_INIT* initid)
{
	UNUSED(initid);
}

const char* bin_to_uuid(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* is_error)
{
	UNUSED(initid);

	if (args->args[0] == NULL) {
		*is_null = 1;
		return NULL;
	}

	if (args->lengths[0] != 16) {
		*is_error = 1;
		return NULL;
	}

	bool swap_flag = args->arg_count == 2 ? *reinterpret_cast<long long*>(args->args[1]) : false;

	char data[16];
	memcpy(data, args->args[0], 16);

	if (swap_flag)
		unswap_ts(data);

	uuid_hexlify(data, result);
	*length = 36;
	return result;
}

#endif
