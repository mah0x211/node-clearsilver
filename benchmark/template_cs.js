var util = require('util'),
fs = require('fs'),
ClearSilver = require('ClearSilver'),
cs = new ClearSilver(),
filepath = __dirname + '/hello_cs.txt',
RENDER_TYPE = process.argv[2],
RENDER_COUNT = ( Number( process.argv[3] ) ) ? +process.argv[3] : 0;

function RenderString( tmpl )
{
    fs.readFile( tmpl, function( err, data )
		{
		if( err ){
		throw err;
		}
		else
		{
		var ms = +new Date();
		
		for( var i = 0; i < RENDER_COUNT; i++ ){
		cs.RenderString( data, { items:['foo', 'bar', 'baz'] } );
		}
		console.log( 'RenderString[' + i + ']: ' + ( +new Date() - ms ) + 'ms' + "\n" + util.inspect( process.memoryUsage() ) );
		// console.log( cs.CACHE );
		}
		});
}

function RenderFile( tmpl )
{
    var ms = +new Date(),
    cnt = 0,
    render = function()
    {
	cs.RenderFile( tmpl, { items:['foo', 'bar', 'baz'] }, function( err, str )
		      {
		      if( err ){
		      throw err;
		      }
		      if( ++cnt === RENDER_COUNT ){
		      console.log( 'RenderFile[' + cnt + ']: ' + ( +new Date() - ms ) + 'ms' + "\n" + util.inspect( process.memoryUsage() ) );
		      // console.log( cs.CACHE );
		      }
		      else {
		      render();
		      }
		      });
    };
    
    render();
}

if( RENDER_TYPE === 'string' ){
    RenderString( filepath );
}
else if( RENDER_TYPE === 'file' ){
    RenderFile( filepath );
}
else {
    console.log( 'unknown render type: ' + RENDER_TYPE );
}
