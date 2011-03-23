var util = require('util'),
	fs = require('fs'),
	ejs = require('ejs');

function Render( tmpl )
{
	var n = ( Number( process.argv[2] ) ) ? +process.argv[2] : 0;
	var ms = +new Date();

	for( var i = 0; i < n; i++ ){
		ejs.render( tmpl, { locals:{ names: ['foo', 'bar', 'baz'] } } );
	}
	console.log( +new Date() - ms + 'ms' + "\n" + util.inspect( process.memoryUsage() ) );	
}

fs.readFile( __dirname + '/hello_ejs.txt', function( err, data ){
	if( err ){
		throw err;
	}
	else {
		Render( data.toString() );
	}
} );
