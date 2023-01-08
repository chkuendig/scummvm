#! /bin/sh

echo -e "#include <new>\ $1 {} " | g++ -x c++ -S - -o- | grep "^_.*:$" | sed -e 's/:$//'
