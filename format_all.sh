#!/bin/bash

clang-format -i `find include/ -type f -name *.hpp`
clang-format -i `find lib/ -type f -name *.cpp`
clang-format -i `find example/ -type f -name *.cpp`
clang-format -i `find test/ -type f -name *.cpp`
