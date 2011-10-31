/*
 binding to ClearSilver
 author: masatoshi teruya
 email: mah0x211@gmail.com
 copyright (C) 2011, masatoshi teruya. all rights reserved.
 */
var pkg = {
	fs:require('fs'),
	path: require('path')
    },
    binding = require( __dirname + '/../build/default/clearsilver_binding'),
    CACHE = {};

function ClearSilver()
{
    var self = this,
	docroot = '',
	addCache = function( filepath, callback )
	{
	    var isFunc = ( typeof callback === 'function' );
	    
	    // create cache
	    pkg.fs.stat( filepath, function( err, stats )
	    {
		if( err )
		{
		    if( isFunc ){
			callback( err );
		    }
		    else {
			console.log( err );
		    }
		}
		else if( !stats.isFile() )
		{
		    err = new Error( filepath + ' is not regular file' );
		    if( isFunc ){
			callback( err );
		    }
		    else {
			console.log( err );
		    }
		}
		else if( CACHE[filepath] && CACHE[filepath].mtime === stats.mtime )
		{
		    if( isFunc ){
			callback( undefined, CACHE[filepath].data );
		    }
		}
		else
		{
		    var cache = { mtime: stats.mtime };
		    pkg.fs.readFile( filepath, function( err, data )
		    {
			if( err )
			{
			    if( isFunc ){
				callback( err );
			    }
			    else {
				console.log( err );
			    }
			}
			else
			{
			    cache.data = data.toString();
			    CACHE[filepath] = cache;
			    if( isFunc ){
				callback( undefined, cache.data );
			    }
			}
		    });
		}
	    });
	},
	cbFileCache = function( filepath, callback ){
	    var cache = ( CACHE[filepath] && CACHE[filepath].data ) ? CACHE[filepath].data : undefined;
	    // add cache
	    addCache( filepath, callback );
	    return cache;
	};
	cbPathResolver = function( filepath ){
	    filepath = self.docroot + pkg.path.normalize( ( ( filepath.charAt(0) != '/' ) ? '/' : '' ) + filepath );
	    return filepath;
	};
    
    this.cs = new binding.Renderer();
    this.cs.registerFileCache( cbFileCache );
    
    this.__defineGetter__( 'DocumentRoot', function(){
	return docroot;
    });
    this.__defineSetter__( 'DocumentRoot', function( DocumentRoot )
    {
	if( typeof DocumentRoot === 'string' ){
	    docroot = DocumentRoot;
	    self.cs.registerPathResolver( cbPathResolver );
	}
	else {
	    docroot = '';
	    self.cs.registerPathResolver( null );
	}
    });
    this.__defineGetter__( 'PathResolver', function(){
	return cbPathResolver;
    });
    this.__defineGetter__( 'FileCache', function(){
	return cbFileCache;
    });
    this.__defineGetter__( 'CACHE', function(){
	return CACHE;
    });
};

ClearSilver.prototype.RenderString = function( str, obj, callback )
{
    var self = this,
    hdf = '';
    
    if( typeof str === 'object' ){
	str = str.toString('utf8');
    }
    if( typeof obj === 'string' ){
	hdf = obj;
    }
    else
    {
	var obj2hdf = function( obj )
	{
	    var type;
	    
	    for( var p in obj )
	    {
		if( ( type = typeof obj[p] ) !== 'function' )
		{
		    hdf += p;
		    if( type === 'boolean' ){
			hdf += " = " + ( ( obj[p] ) ? '1' : '0' ) + "\n";
		    }
		    else if( type === 'number' ){
			hdf += " = " + +obj[p] + "\n";
		    }
		    else if( type === 'object' )
		    {
			if( obj[p].constructor === Date ){
			    hdf += " = " + +obj[p] + "\n";
			}
			else {
			    hdf += " {\n";
			    obj2hdf( obj[p] );
			    hdf += "\n}\n";
			}
		    }
		    else if( type === 'string' && obj[p].match("\n") ){
			hdf += " << EOM\n" + obj[p] + "\nEOM\n";
		    }
		    else {
			hdf += " = " + obj[p] + "\n";
		    }
		}
	    }
	};
	obj2hdf( obj );
    }
    
    if( callback ){
	process.nextTick( function(){
	    callback( undefined, self.cs.renderString( str, hdf ) );
	});
    }
    else {
	return this.cs.renderString( str, hdf );
    }
};

ClearSilver.prototype.RenderFile = function( path, obj, callback )
{
    var self = this;
    
    this.FileCache( path, function( err, data )
    {
	if( err ){
	    callback( err );
	}
	else {
	    callback( undefined, self.RenderString( data, obj ) );
	}
    });
};

module.exports = ClearSilver;
