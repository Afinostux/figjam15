
echo "=====jam game=====" > errors.err
clang main.cpp -g -lm -lSDL2 -o jamgame 2>>errors.err
