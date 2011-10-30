#!/bin/sh

CSLIBS=./cslibs
SHPLIB=streamhtmlparser-0.1.tar.gz

if [ -d $CSLIBS ]; then 
    rm -Rf $CSLIBS
fi
svn checkout http://clearsilver.googlecode.com/svn/trunk/ $CSLIBS

echo "cd $CSLIBS"
cd $CSLIBS
CSLIBS=`pwd`
mkdir ./libs
wget http://streamhtmlparser.googlecode.com/files/streamhtmlparser-0.1.tar.gz
tar xvzf streamhtmlparser-0.1.tar.gz
mv ./streamhtmlparser-0.1 ./streamhtmlparser
./autogen.sh
./configure --prefix=$CSLIBS --disable-apache --disable-python --disable-perl --disable-ruby --disable-java --disable-csharp
make depend
make && make man && make install
cd ../
node-waf configure --clearsilver=$CSLIBS/lib --clearsilver-includes=$CSLIBS/includes
node-waf build
