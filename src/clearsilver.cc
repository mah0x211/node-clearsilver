#include <node.h>
#include <node_events.h>

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <cstring>
#include "ClearSilver/ClearSilver.h"

using namespace v8;
using namespace node;

static inline char *CHECK_NEOERR( NEOERR *ec )
{
	if( ec != STATUS_OK )
	{
		char *retval = NULL;
		STRING estr;
		
		string_init(&estr);
		nerr_error_string( ec, &estr );
		asprintf( &retval, "%s", estr.buf );
		string_clear( &estr );
		free( ec );
		
		return retval;
	}
	
	return NULL;
}

class ClearSilver : public ObjectWrap
{
	public:
		ClearSilver(){};
		~ClearSilver(){};
		static void Initialize( Handle<Object> target);
		
	private:
		// path resolver
		Handle<Function> resolver;
		
		static Handle<Value> New (const Arguments& args);
		static NEOERR *hookFileload( void *ctx, HDF *hdf, const char *filepath, char **inject );
		static NEOERR *cbRender( void *ctx, char *str );
		char *RenderString( STRING *page, const char *str, char *val );
		static Handle<Value> renderString( const Arguments &args );
		static Handle<Value> pathResolver( const Arguments &args );
};

Handle<Value> ClearSilver::New( const Arguments& args )
{
	HandleScope scope;
	ClearSilver *cs = new ClearSilver();
	
	cs->Wrap( args.Holder() ); 
	cs->resolver = Handle<Function>::Cast( Null() );
	return args.This();
}


NEOERR *ClearSilver::hookFileload( void *ctx, HDF *hdf, const char *filepath, char **inject )
{
	HandleScope scope;
	ClearSilver *cs = (ClearSilver*)ctx;
	// call path-resolver
	Handle<Value> argv[1] = { String::New( filepath ) };
	Handle<Value> retval = cs->resolver->Call( Context::GetCurrent()->Global(), 1, argv );
	
	if( retval->IsNull() ){
		asprintf( inject, "[include] failed to include: %s", filepath );
	}
	else
	{
		// no need to free
		const char *filepath_resolve = *String::Utf8Value( retval );
		char *ext = rindex( filepath_resolve, '.' );
		char *estr = NULL;
		
		if( strcmp( ext, ".hdf" ) == 0 )
		{
			if( ( estr = CHECK_NEOERR( hdf_read_file( hdf, filepath_resolve ) ) ) ){
				asprintf( inject, "[include] failed to include %s: %s\n", filepath, estr );
				free( estr );
			}
			else {
				asprintf( inject, "" );
			}
		}
		else if( ( estr = CHECK_NEOERR( ne_load_file( filepath_resolve, inject ) ) ) ){
			asprintf( inject, "[include] failed to include %s: %s\n", filepath, estr );
			free( estr );
		}
	}
	return STATUS_OK;
}

NEOERR *ClearSilver::cbRender( void *ctx, char *str )
{
	if( str ){
		STRING *page = (STRING*)ctx;
		string_append( page, str );
	}
	
	return STATUS_OK;
}

char *ClearSilver::RenderString( STRING *page, const char *str, char *val )
{
	char *estr = NULL;
	size_t len = strlen( str );
	char *parse = (char*)malloc( sizeof(char) * (len + 1) );
	
	if( !parse || !memcpy( (void*)parse, (const void*)str, len ) ){
		estr = strerror( errno );
	}
	else
	{
		NEOERR *ec = NULL;
		HDF *hdf = NULL;
		CSPARSE *csp = NULL;
		
		parse[len] = 0;
		// register function and hook
		if( !( estr = CHECK_NEOERR( hdf_init( &hdf ) ) ) &&
			( !val || !( estr = CHECK_NEOERR( hdf_read_string( hdf, val ) ) ) ) &&
			!( estr = CHECK_NEOERR( cs_init( &csp, hdf ) ) ) &&
			!( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"url_escape", cgi_url_escape ) ) ) &&
			!( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"html_escape", cgi_html_escape_strfunc ) ) ) &&
			!( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"text_html", cgi_text_html_strfunc ) ) ) && 
			!( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"js_escape", cgi_js_escape ) ) ) && 
			!( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"html_strip", cgi_html_strip_strfunc ) ) ) )
		{
			if( this->resolver->IsFunction() ){
				cs_register_fileload( csp, (void*)this, hookFileload );
			}
		}
		// parse and render
		if( !estr &&
			!( estr = CHECK_NEOERR( cs_parse_string( csp, parse, len ) ) ) ){
			estr = CHECK_NEOERR( cs_render( csp, page, cbRender ) );
		}
		
		if( hdf ){
			hdf_destroy( &hdf );
		}
		if( parse ){
			cs_destroy( &csp );
		}
		else{
			free( parse );
		}
	}
	
	return estr;
}


Handle<Value> ClearSilver::renderString( const Arguments &args )
{
	HandleScope scope;
	Handle<Value> retval = Null();
	
	if( args.Length() < 1 || args[0]->IsString() == false ){
		retval = ThrowException( Exception::TypeError( String::New( "renderString( template:string, hdf:string" ) ) );
	}
	else
	{
		ClearSilver *cs = ObjectWrap::Unwrap<ClearSilver>(args.This());
		STRING page;
		char *estr;
		
		string_init( &page );
		// hdf string
		estr = cs->RenderString( &page, *String::Utf8Value( args[0] ), ( args[1]->IsString() ) ? *String::Utf8Value( args[1] ) : NULL );
		
		if( estr ){
			retval = ThrowException( Exception::Error( String::New( estr ) ) );
			free( estr );
		}
		else if( page.len ){
			retval = Encode( page.buf, page.len, UTF8 );
		}
		string_clear( &page );
	}
	
	return retval;
}

Handle<Value> ClearSilver::pathResolver( const Arguments &args )
{
	HandleScope scope;
	ClearSilver *cs = ObjectWrap::Unwrap<ClearSilver>(args.This());
	
	if( args.Length() < 1 || !args[0]->IsFunction() )
	{
		// ???: need free?
		if( !cs->resolver->IsNull() ){
			cs->resolver = Handle<Function>::Cast( Null() );
		}
	}
	else {
		cs->resolver = Persistent<Function>::New( Local<Function>::Cast( args[0] ) );
	}
	
	return Null();
}

void ClearSilver::Initialize( Handle<Object> target )
{
	HandleScope scope;
	Local<FunctionTemplate> t = FunctionTemplate::New( New );
	
	t->SetClassName( String::NewSymbol("Renderer") );
	t->InstanceTemplate()->SetInternalFieldCount(1);
	NODE_SET_PROTOTYPE_METHOD( t, "renderString", renderString );
	NODE_SET_PROTOTYPE_METHOD( t, "pathResolver", pathResolver );
	target->Set( String::NewSymbol("Renderer"), t->GetFunction() );
}

extern "C" void init( Handle<Object> target )
{
	HandleScope scope;
	ClearSilver::Initialize( target );
};
