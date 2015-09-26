
echo "=====jam game=====" > errors.err
clang main.cpp -g -lm -lSDL2 -lSDL2_mixer -o jamgame 2>>errors.err
