# MySQL uuid to binary UDF functions

### General info

MySQL UDF functions implemented in C++ for storing UUID's in the optimal way as described in [MariaDB blog][1] and [Percona blog][2].

MySQL 8.0 has implemented these functions. These UDF functions provide the same functionality for earlier versions, and for MariaDB which is planning to [not implement][6] these functions in favour of a [UUID data type][7]. Once those changes are made these functions may not be needed if they provide these function names as syntactic sugar around the CAST syntax.

As described in the above articles there are few problems with UUID:
- UUID has 36 characters which makes it bulky.
- InnoDB stores data in the PRIMARY KEY order and all the secondary keys also contain PRIMARY KEY. So having UUID as PRIMARY KEY makes the index bigger which can not be fit into the memory
- Inserts are random and the data is scattered.

The articles explains how to store UUID in an efficient way by re-arranging timestamp part of UUID.

#### API

This module includes two functions to convert a UUID into the ordered binary format and the other way around.

The functions are:
- `is_uuid` - check if a string is a valid UUID of the 3 formats supported by MySQL 8.0
- `uuid_to_bin` - convert a uuid string into the binary format, optionally performing timestamp bit swapping
- `bin_to_uuid` - convert the binary format into the uuid string, optionally performing timestamp bit swapping

The output of bin_to_uuid is the dashed format to be compatible with the MySQL 8.0 version.

#### Compilation

Just run `make` in the project root. This should work on linux and Mac OS X. Compiling scripts are not
tested for other platforms.

#### Installation

After you compile you must install it and tell MySQL about it ([More details][4]).

First you need to locate the plugin directory. This directory is given by the value of the `plugin_dir` system variable.
Usually located in `/usr/lib/mysql/plugin/` in linux. Use `SHOW VARIABLES LIKE 'plugin_dir';` to locate this.

- Copy the shared object to the server's plugin directory (see above) and name it `uuid2bin.so`
- Inform mysql about the new functions by running:

```sql
CREATE FUNCTION is_uuid RETURNS INTEGER SONAME 'uuid2bin.so';
CREATE FUNCTION uuid_to_bin RETURNS STRING SONAME 'uuid2bin.so';
CREATE FUNCTION bin_to_uuid RETURNS STRING SONAME 'uuid2bin.so';
```

#### Testing

```
SELECT IS_UUID('6ccd780c-baba-1026-9564-0040f4311e29') = 1;
SELECT IS_UUID('6ccd780cbaba102695640040f4311e29') = 1;
SELECT IS_UUID('{6ccd780c-baba-1026-9564-0040f4311e29}') = 1;
SELECT IS_UUID(NULL) IS NULL;
SELECT IS_UUID(123) = 0;
SELECT IS_UUID('invalid') = 0;
SELECT BIN_TO_UUID(UUID_TO_BIN('6ccd780c-baba-1026-9564-0040f4311e29')) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('6ccd780c-baba-1026-9564-0040f4311e29', 0), 0) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('6ccd780c-baba-1026-9564-0040f4311e29', 1), 1) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('6ccd780cbaba102695640040f4311e29', 0), 0) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('6ccd780cbaba102695640040f4311e29', 1), 1) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('{6ccd780c-baba-1026-9564-0040f4311e29}', 0), 0) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN('{6ccd780c-baba-1026-9564-0040f4311e29}', 1), 1) = '6ccd780c-baba-1026-9564-0040f4311e29';
SELECT BIN_TO_UUID(UUID_TO_BIN(NULL)) IS NULL;
SELECT BIN_TO_UUID(UUID_TO_BIN('invalid')) IS NULL;
```

#### Benchmarks

We run 10 million times each function and compare the results with the same methods implemented as stored functions.

Performance was also compared to an alternative implementation by [silviucpp][5] patched with UUID_TO_BIN_OLD and BIN_TO_UUID_OLD names to avoid a conflict.

###### Stored functions version:

```sql
DELIMITER //

CREATE FUNCTION UuidToBin(_uuid BINARY(36))
        RETURNS BINARY(16)
        LANGUAGE SQL  DETERMINISTIC  CONTAINS SQL  SQL SECURITY INVOKER
    RETURN
        UNHEX(CONCAT(SUBSTR(_uuid, 15, 4), SUBSTR(_uuid, 10, 4),
                     SUBSTR(_uuid,  1, 8), SUBSTR(_uuid, 20, 4),
                     SUBSTR(_uuid, 25) ));

CREATE FUNCTION UuidFromBin(_bin BINARY(16))
        RETURNS BINARY(36)
        LANGUAGE SQL  DETERMINISTIC  CONTAINS SQL  SQL SECURITY INVOKER
    RETURN
        LCASE(CONCAT_WS('-', HEX(SUBSTR(_bin,  5, 4)), HEX(SUBSTR(_bin,  3, 2)),
                             HEX(SUBSTR(_bin,  1, 2)), HEX(SUBSTR(_bin,  9, 2)),
                             HEX(SUBSTR(_bin, 11))));
//
DELIMITER ;
```

###### Results

```
SET @loops=10000000;
SET @uuid=UUID();

SELECT BENCHMARK(@loops, BIN_TO_UUID(UUID_TO_BIN(@uuid, 0), 0));
SELECT BENCHMARK(@loops, BIN_TO_UUID(UUID_TO_BIN(@uuid, 1), 1));
SELECT BENCHMARK(@loops, BIN_TO_UUID_OLD(UUID_TO_BIN_OLD(@uuid)));
SELECT BENCHMARK(@loops, UuidFromBin(UuidToBin(@uuid)));

```

|Function                           | Time for 1M executions    |
|:---------------------------------:|:-------------------------:|
| UUID_TO_BIN + BIN_TO_UUID noswap  | 9.47 sec                  |
| UUID_TO_BIN + BIN_TO_UUID swap    | 9.79 sec                  |
| UUID_TO_BIN_OLD + BIN_TO_UUID_OLD | 30.96 sec                 |
| UuidToBin + UuidFromBin           | 9 min 31.44 sec           |

###### Conclusion

As expected the UDF versions are much faster than the stored functions. They are also a third of the speed of the earlier implementation.

Timestamp bit swapping understandably incurs a performance penalty of ~5%. However the greatly improved index performance should more than compensate for this.

[1]:https://mariadb.com/kb/en/library/guiduuid-performance/
[2]:https://www.percona.com/blog/2014/12/19/store-uuid-optimized-way/
[3]:https://dev.mysql.com/doc/refman/8.0/en/miscellaneous-functions.html
[4]:http://dev.mysql.com/doc/refman/5.7/en/udf-compiling.html
[5]:https://github.com/silviucpp/uuid2bin 
[6]:https://jira.mariadb.org/browse/MDEV-15854
[7]:https://jira.mariadb.org/browse/MDEV-4958
