
    && echo "CREATE FUNCTION is_uuid RETURNS INTEGER SONAME 'uuid2bin.so';" >> /docker-entrypoint-initdb.d/uuid2bin.sql \
    && echo "CREATE FUNCTION uuid_to_bin RETURNS STRING SONAME 'uuid2bin.so';" >> /docker-entrypoint-initdb.d/uuid2bin.sql \
    && echo "CREATE FUNCTION bin_to_uuid RETURNS STRING SONAME 'uuid2bin.so';" >> /docker-entrypoint-initdb.d/uuid2bin.sql
