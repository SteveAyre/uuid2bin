FROM mariadb:latest

COPY ./ /usr/src/uuid2bin/

RUN    apt-get update \
    && apt-get install -y make g++ libmysqlclient-dev \
    && apt-get clean \
    \
    && make -C /usr/src/uuid2bin \
    && install -m644 -o0 -g0 /usr/src/uuid2bin/uuid2bin.so /usr/lib/mysql/plugin/ \
    && install -m755 -o0 -g0 /usr/src/uuid2bin/install.sh /docker-entrypoint-initdb.d/ \
    && rm -rf /usr/src/uuid2bin \
    \
    && dpkg -P make g++ libmysqlclient-dev \
    && apt-get autoremove -y

