#!/bin/sh

set -e
CWD=`pwd`
SHPLIB='streamhtmlparser-0.1.tar.gz'
DEPEND="$CWD/depend"
CSLIBS='./cslibs'
CONF="--prefix=$DEPEND --disable-apache --disable-python --disable-perl --disable-ruby --disable-java --disable-csharp --with-streamhtmlparser=./streamhtmlparser-0.1"

if [ ! -d $CSLIBS ]; then
    echo "svn checkout http://clearsilver.googlecode.com/svn/trunk/ $CSLIBS"
    svn checkout http://clearsilver.googlecode.com/svn/trunk/ $CSLIBS
    if [ ! -d $CSLIBS ]; then 
        exit -1
    fi
fi

echo "cd $CSLIBS"
cd $CSLIBS

if [ ! -d ./libs ]; then
    echo 'mkdir ./libs'
    mkdir ./libs
fi

if [ ! -d 'streamhtmlparser-0.1' ]; then
    echo "wget http://streamhtmlparser.googlecode.com/files/$SHPLIB"
    wget http://streamhtmlparser.googlecode.com/files/$SHPLIB
    if [ ! -f $SHPLIB ]; then
        exit -1;
    else
        echo "tar xzf $SHPLIB"
        tar xzf $SHPLIB
        rm $SHPLIB
    fi
fi

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.
aclocal -I m4
autoheader
autoconf
echo "./configure $CONF"
./configure $CONF

echo "make depend && make && make man && make install"
make man && make depend && make install
echo "cd $CWD"
cd $CWD

node-waf configure --clearsilver=$DEPEND
node-waf build
