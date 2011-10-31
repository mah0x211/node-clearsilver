#!/bin/sh

CSLIBS=./cslibs
SHPLIB=streamhtmlparser-0.1.tar.gz

if [ -d $CSLIBS ]; then 
    rm -Rf $CSLIBS
fi
echo "svn checkout http://clearsilver.googlecode.com/svn/trunk/ $CSLIBS"
svn checkout http://clearsilver.googlecode.com/svn/trunk/ $CSLIBS

echo "cd $CSLIBS"
cd $CSLIBS

CSLIBS=`pwd`
echo 'mkdir ./libs'
mkdir ./libs

echo 'wget http://streamhtmlparser.googlecode.com/files/streamhtmlparser-0.1.tar.gz'
wget http://streamhtmlparser.googlecode.com/files/streamhtmlparser-0.1.tar.gz

echo 'tar xvzf streamhtmlparser-0.1.tar.gz'
tar xvzf streamhtmlparser-0.1.tar.gz

echo 'mv ./streamhtmlparser-0.1 ./streamhtmlparser'
mv ./streamhtmlparser-0.1 ./streamhtmlparser

echo './autogen.sh'
./autogen.sh

echo "./configure --prefix=$CSLIBS --disable-apache --disable-python --disable-perl --disable-ruby --disable-java --disable-csharp"
./configure --prefix=$CSLIBS --disable-apache --disable-python --disable-perl --disable-ruby --disable-java --disable-csharp
make depend && make && make man && make install
echo 'cd ../'
cd ../

CSLIBS=`pwd`
node-waf configure --clearsilver=$CSLIBS/cslibs/lib --clearsilver-includes=$CSLIBS/cslibs/include
node-waf build
