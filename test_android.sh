#!/bin/sh

set -e

DIR=/data/dan

echo "MAKE testapp..."
make testapp

echo "PUSH testapp..."
adb push libload.so $DIR
adb push testapp $DIR

echo "CREATE testapp.sh..."
echo "#!/system/bin/sh" > testapp.sh
echo "# Android loader ignores RPATH =(" >> testapp.sh
echo "LD_LIBRARY_PATH=$DIR exec $DIR/testapp \$1 \$2 \$3 \$4" >> testapp.sh
adb push testapp.sh $DIR
adb shell chmod 777 $DIR/testapp.sh

#echo "RUN testapp.sh..."
#adb shell $DIR/testapp.sh
