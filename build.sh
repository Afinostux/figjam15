
echo "=====jam game=====" > errors.err
clang main.cpp -g -lm -lSDL2 -lSDL2_mixer -lSDL2_image -o jamgame 2>>errors.err
