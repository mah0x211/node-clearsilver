#include <node.h>
#include <node_events.h>

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <limits.h>

#include <cstring>
#include <typeinfo>
// #include <pthread.h>

#include "ClearSilver/ClearSilver.h"

using namespace v8;
using namespace node;

#define ObjectUnwrap(tmpl,obj)  ObjectWrap::Unwrap<tmpl>(obj)
#define IsDefined(v) ( !v->IsNull() && !v->IsUndefined() )

// YYYY-MM-DDThh:mm:ss[.%03dmsec]Z and NULL
#define ISO8601_STRING_LEN 25
#define MSEC_TO_ISO8601(str,t)({ \
    time_t epoch = t / 1000; \
    int32_t msec = t % 1000; \
    struct tm tm; \
    memset( str, 0, ISO8601_STRING_LEN ); \
    gmtime_r( &epoch, &tm ); \
    snprintf( str, ISO8601_STRING_LEN, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec ); \
})

typedef enum  JS_TYPEOF{
    JS_TYPE_BOOLEAN = 0,
    JS_TYPE_NUMBER = 1,
    JS_TYPE_STRING = 2,
    JS_TYPE_OBJECT = 3,
    JS_TYPE_ARRAY = 4,
    JS_TYPE_DATE = 5,
    JS_TYPE_REGEXP = 6,
    JS_TYPE_FUNCTION = 7,
    JS_TYPE_NULL = 8,
    JS_TYPE_UNDEFINED = 9,
};

typedef enum  JS_TYPEOF_BIT {
    JS_TYPE_BOOLEAN_BIT = 1 << 0,
    JS_TYPE_NUMBER_BIT = 1 << 1,
    JS_TYPE_STRING_BIT = 1 << 2,
    JS_TYPE_OBJECT_BIT = 1 << 3,
    JS_TYPE_ARRAY_BIT = 1 << 4,
    JS_TYPE_DATE_BIT = 1 << 5,
    JS_TYPE_REGEXP_BIT = 1 << 6,
    JS_TYPE_FUNCTION_BIT = 1 << 7,
    JS_TYPE_NULL_BIT = 1 << 8,
    JS_TYPE_UNDEFINED_BIT = 1 << 9,
};

typedef struct {
    const char *const name;
    size_t len;
    int flag;
} JS_TYPE;

static const JS_TYPE TYPE_NAMES[] = {
    { "[object Boolean]", strlen("[object Boolean]"), JS_TYPE_BOOLEAN_BIT },
    { "[object Number]", strlen("[object Number]"), JS_TYPE_NUMBER_BIT },
    { "[object String]", strlen("[object String]"), JS_TYPE_STRING_BIT },
    { "[object Object]", strlen("[object Object]"), JS_TYPE_OBJECT_BIT },
    { "[object Array]", strlen("[object Array]"), JS_TYPE_ARRAY_BIT },
    { "[object Date]", strlen("[object Date]"), JS_TYPE_DATE_BIT },
    { "[object RegExp]", strlen("[object RegExp]"), JS_TYPE_REGEXP_BIT },
    { "[object Function]", strlen("[object Function]"), JS_TYPE_FUNCTION_BIT },
    NULL
};

#define isPrintable  (JS_TYPE_BOOLEAN_BIT|JS_TYPE_STRING_BIT|JS_TYPE_DATE_BIT|JS_TYPE_NUMBER_BIT)
#define isRecursive  (JS_TYPE_OBJECT_BIT|JS_TYPE_ARRAY_BIT)
#define isRemoval    (JS_TYPE_NULL_BIT|JS_TYPE_UNDEFINED_BIT)

typedef struct ParseCtx_t ParseCtx_t;

typedef struct {
    void *id;
    void *data;
} KeyVal_t;

typedef struct {
    void *ctx;
    // callback js function when async is true
    Persistent<Function> callback;
    NEOERR *nerr;
    void *data;
    eio_req *req;
} Baton_t;


static inline char *TypeName( Handle<Value> v )
{
    if( v->IsNull() || v->IsUndefined() ){
        return NULL;
    }
    else {
        Local<Object> obj = v->ToObject();
        return *String::Utf8Value( obj->ObjectProtoToString() );
    }
}

static inline uint32_t TypeOf( Handle<Value> v )
{
    uint32_t flag = 0;
    
    if( v->IsNull() ){
        flag |= JS_TYPE_NULL_BIT;
    }
    else if( v->IsUndefined() ){
        flag |= JS_TYPE_UNDEFINED_BIT;
    }
    else
    {
        Local<Object> obj = v->ToObject();
        char *proto = *String::Utf8Value( obj->ObjectProtoToString() );
        
        for( int i = 0; i < JS_TYPE_NULL; i++ )
        {
            if( !strncmp( TYPE_NAMES[i].name, proto, TYPE_NAMES[i].len ) ){
                flag |= TYPE_NAMES[i].flag;
                break;
            }
        }
    }
    return flag;
}

static inline bool IsTypeOf( Handle<Value> v, JS_TYPEOF type_id )
{
    if( JS_TYPE_NULL == type_id && v->IsNull() ){
        return true;
    }
    else if( JS_TYPE_UNDEFINED == type_id && v->IsUndefined() ){
        return true;
    }
    else if( type_id < JS_TYPE_NULL ){
        Local<Object> obj = v->ToObject();
        JS_TYPE type = TYPE_NAMES[type_id];
        return !strncmp( type.name, *String::Utf8Value( obj->ObjectProtoToString() ), type.len );
    }
    
    return false;
}


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

static inline int CurrentTimestamp( char **str )
{
    struct timeval tv;
    
    // ???: needs lock/mutex?
    if( 0 != gettimeofday( &tv, NULL ) || -1 == asprintf( str, "%lld%06d", tv.tv_sec, tv.tv_usec ) ){
        *str = NULL;
        return errno;
    }
    
    return 0;
}


// MARK: @interface
class ClearSilver : public ObjectWrap
{
    // MARK: @public
    public:
        ClearSilver(){};
        ~ClearSilver();
        static void Initialize( Handle<Object> target );
    // MARK: @private
    private:
        HDF *hdf;
        CSPARSE *csp;
        pthread_mutex_t mutex;
        // cache
        NE_HASH *parseCache;
        NE_HASH *fileCache;
        // TODO: impl cache control
        // static Handle<Value> cachedParsers( const Arguments &argv );
        // static Handle<Value> cachedFiles( const Arguments &argv );
        
        // new
        static Handle<Value> New( const Arguments& argv );
        // parser createParser
        Handle<Value> _createParser( Handle<Value> id, ParseCtx_t **context );
        static Handle<Value> createParser( const Arguments &argv );
        static Handle<Value> removeParser( const Arguments &argv );
        static Handle<Value> parseString( const Arguments &argv );
        
        // render
        static int renderBeginEIO( eio_req *req );
        static int renderEndEIO( eio_req *req );
        static Handle<Value> render( const Arguments &argv );
        
        // callback and hook
        static NEOERR *callbackRender( void *ctx, char *str );
        static NEOERR *hookFileload( void *ctx, HDF *hdf, const char *filepath, char **inject );
    
        // setter/getter
        static Handle<Value> _setValue( HDF *hdf, Local<Object> obj, const char *const parentKey, Local<Array> refs );
        static Handle<Value> setValue( const Arguments& argv );
        static Handle<Value> getValue( const Arguments& argv );
        static Handle<Value> removeValue( const Arguments& argv );
        static Handle<Value> dump( const Arguments &argv );

        // TODO: impl parseFile
        // static Handle<Value> parseFile( const Arguments &argv );
        // static int parseStringBeginEIO( eio_req *req );
        // static int parseStringEndEIO( eio_req *req );
        // TODO: impl HDF setter/getter
        // static Handle<Value> parseString( const Arguments &argv );
};

struct ParseCtx_t {
    const char *id;
    ClearSilver *cs;
    HDF *hdf;
    CSPARSE *csp;
    pthread_mutex_t mutex;
};

static ParseCtx_t *CreateContext( const char *id, char **estr )
{
    ParseCtx_t *ctx = NULL;
    
    if( !( ctx = (ParseCtx_t*)calloc( 1, sizeof( ParseCtx_t ) ) ) ){
        asprintf( estr, "%s", strerror(errno) );
        return NULL;
    }
    // init hdf
    else if( ( *estr = CHECK_NEOERR( hdf_init( &ctx->hdf ) ) ) ){
        free( ctx );
        return NULL;
    }
    else if( id )
    {
        if( -1 == asprintf( (char**)&ctx->id, "%s", id ) ){
            asprintf( estr, "%s", strerror(errno) );
            hdf_destroy(&ctx->hdf);
            free(ctx);
            return NULL;
        }
    }
    else
    {
        if( 0 != CurrentTimestamp( (char**)&ctx->id ) ){
            asprintf( estr, "%s", strerror(errno) );
            hdf_destroy(&ctx->hdf);
            free( ctx );
            return NULL;
        }
    }
    return ctx;
}

static inline void DestroyContext( ParseCtx_t *ctx )
{
    if( ctx )
    {
        pthread_mutex_lock( &ctx->mutex );
        //printf( "    id: %s\n", ctx->id );
        if( ctx->id ){
            free( (void*)ctx->id );
        }
        if( ctx->csp ){
            //printf( "    csp: %p\n", ctx->csp );
            cs_destroy(&ctx->csp);
        }
        //printf( "    hdf: %p\n", ctx->hdf );
        hdf_destroy(&ctx->hdf);
        pthread_mutex_unlock( &ctx->mutex );
        pthread_mutex_destroy(&ctx->mutex);
        //printf( "    free ctx: %p\n", ctx );
        free( ctx );
    }
}


// MARK: @implements

ClearSilver::~ClearSilver()
{
    NE_HASHNODE *node = NULL;
    NE_HASHNODE *next = NULL;
    UINT32 bkt = 0;
    int ndestroy = 0;
    struct timespec rem = { 0, 90000000 };
    
    //printf("destroy\n");
    // cleanup parse cache
    for( bkt = 0; bkt < parseCache->size; bkt++ )
    {
        if( ( node = parseCache->nodes[bkt] ) )
        {
            // printf("stepin\n");
            while (node)
            {
                next = node->next;
                // printf( "destroy parse cache: key->%s, val->%p\n", (char*)node->key, node->value );
                ndestroy++;
                // nanosleep( &rem, NULL );
                void *data = ne_hash_remove( parseCache, node->key );
                //printf( "DestroyContext[%d]: %p\n", ndestroy, (char*)node->key );
                DestroyContext( (ParseCtx_t*)data );
                node = next;
            }
        }
    }
    ne_hash_destroy( &parseCache );
    // cleanup include cache
    for( bkt = 0; bkt < fileCache->size; bkt++ )
    {
        if( ( node = fileCache->nodes[bkt] ) )
        {
            while (node) {
                next = node->next;
                ne_hash_remove( fileCache, node->key );
                free( node->key );
                free( node->value );
                node = next;
            }
        }
    }
    ne_hash_destroy( &fileCache );
    pthread_mutex_destroy( &mutex );
}

Handle<Value> ClearSilver::New( const Arguments& argv )
{
    HandleScope scope;
    ClearSilver *cs = new ClearSilver();
    const char *estr = NULL;
    Handle<Value> retval = Null();
    
    // init cache
    if( ( estr = CHECK_NEOERR( ne_hash_init( &cs->parseCache, ne_hash_str_hash, ne_hash_str_comp ) ) ) ||
        ( estr = CHECK_NEOERR( ne_hash_init( &cs->fileCache, ne_hash_str_hash, ne_hash_str_comp ) ) ) )
    {
        pthread_mutex_destroy( &cs->mutex);
        if( cs->parseCache ){
            ne_hash_destroy( &cs->parseCache );
        }
        retval = ThrowException( Exception::Error( String::New( estr ) ) );
        free( (void*)estr );
    }
    else {
        pthread_mutex_init( &cs->mutex, NULL );
        cs->Wrap( argv.This() );
        retval = argv.This();
    }
    return scope.Close( retval );
}

// MARK: cache control
/*
NEOERR* ClearSilver::addCache( const char *cache_id, void *cache )
{
    NEOERR *nerr = STATUS_OK;
    char *cache_old = (char*)ne_hash_lookup( fileCache, (void*)cache_id );
    
    if( !cache_old )
    {
        size_t len = strlen( cache_id );
        char *id = (char*)calloc( len+1, sizeof( char ) );
        
        if( !id ){
            nerr = nerr_raise( NERR_NOMEM, "%s", strerror(errno) );
        }
        else
        {
            memcpy( id, cache_id, len );
            if( STATUS_OK != ( nerr = ne_hash_insert( fileCache, (void*)id, (void*)cache ) ) ){
                free(id);
            }
        }
    }
    
    return nerr;
}
*/

// parser_id:String createParser( const char *id )
Handle<Value> ClearSilver::_createParser( Handle<Value> id, ParseCtx_t **context )
{
    Handle<Value> retval = Undefined();
    char *estr = NULL;
    char *parser_id = ( !id->IsString() ) ? NULL : *String::Utf8Value( id );
    ParseCtx_t *ctx = CreateContext( parser_id, &estr );
    
    if( !ctx ){
        retval = ThrowException( Exception::Error( String::New( estr ) ) );
        free( estr );
    }
    // add parse cache
    else if( ( estr = CHECK_NEOERR( ne_hash_insert( parseCache, (void*)ctx->id, (void*)ctx ) ) ) ){
        retval = ThrowException( Exception::Error( String::New( estr ) ) );
        free( estr );
        DestroyContext( ctx );
    }
    else
    {
        // init parser mutex
        pthread_mutex_init( &ctx->mutex, NULL );
        ctx->cs = this;
        // register fileload hook
        cs_register_fileload( ctx->csp, (void*)ctx, hookFileload );
        // return parser_id
        retval = ( parser_id ) ? id : String::New( ctx->id );
        if( context ){
            *context = ctx;
        }
    }
    
    return retval;
}

Handle<Value> ClearSilver::createParser( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    
    // defined parser_id
    if( 0 < argc )
    {
        if( argv[0]->IsString() ){
            retval = cs->_createParser( argv[0], NULL );
        }
        else {
            retval = ThrowException( Exception::TypeError( String::New( "createParser( [parser_id:String] )" ) ) );
        }
    }
    // undefined parser_id
    else {
        retval = cs->_createParser( Null(), NULL );
    }
    
    return scope.Close( retval );
}

Handle<Value> ClearSilver::removeParser( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( 1 > argc || !argv[0]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "removeParser( parser_id:String )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_remove( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to removeValue: parser not found" ) ) );
    }
    else if( pthread_mutex_lock( &cs->mutex ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( strerror(errno) ) ) );
    }
    else
    {
        DestroyContext( ctx );
        if( pthread_mutex_unlock( &cs->mutex ) ){
            retval = ThrowException( Exception::ReferenceError( String::New( strerror(errno) ) ) );
        }
    }
    
    return scope.Close( retval );
}


// parseString( template:String, parser_id:String )
Handle<Value> ClearSilver::parseString( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Null();
    ParseCtx_t *ctx = NULL;
    // flag for delete context when failed to parsing
    bool isTmp = false;
    
    // invalid arguments
    if( !argv[0]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "parseString( template:String, [parser_id:String] )" ) ) );
    }
    // arguments has parser_id
    else if( 1 < argc && IsDefined( argv[1] ) )
    {
        // invalid arguments
        if( !argv[1]->IsString() ){
            retval = ThrowException( Exception::TypeError( String::New( "parseString( template:String, [parser_id:String] )" ) ) );
        }
        // find parser
        else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, *String::Utf8Value( argv[1] ) ) ) ){
            retval = ThrowException( Exception::ReferenceError( String::New( "faild to parseString: parser not found" ) ) );
        }
        // already compiled
        else if( ctx->csp ){
            retval = argv[1];
        }
    }
    else
    {
        // create parser
        Handle<Value> parser_id = cs->_createParser( Null(), &ctx );
        
        isTmp = false;
        // exception
        if( !parser_id->IsString() ){
            retval = parser_id;
        }
    }
    
    if( retval->IsNull() )
    {
        CSPARSE *csp = NULL;
        char *tmpl = NULL;
        size_t len = 0;
        char *estr = NULL;
        
        // copy template string
        if( -1 == ( len = asprintf( (char**)&tmpl, "%s", *String::Utf8Value( argv[0] ) ) ) ){
            retval = ThrowException( Exception::Error( String::New( strerror(errno) ) ) );
        }
        // init csparse and register function
        else if(( estr = CHECK_NEOERR( cs_init( &csp, ctx->hdf ) ) ) ||
                ( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"url_escape", cgi_url_escape ) ) ) ||
                ( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"html_escape", cgi_html_escape_strfunc ) ) ) ||
                ( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"text_html", cgi_text_html_strfunc ) ) ) ||
                ( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"js_escape", cgi_js_escape ) ) ) ||
                ( estr = CHECK_NEOERR( cs_register_strfunc( csp, (char*)"html_strip", cgi_html_strip_strfunc ) ) ) ){
            retval = ThrowException( Exception::Error( String::New( strerror(errno) ) ) );
            free(tmpl);
            cs_destroy(&csp);
        }
        // parse
        else if( ( estr = CHECK_NEOERR( cs_parse_string( csp, tmpl, len ) ) ) ){
            retval = ThrowException( Exception::Error( String::New( estr ) ) );
            free(estr);
            cs_destroy(&csp);
        }
        // success
        else {
            ctx->csp = csp;
            retval = String::New( ctx->id );
        }
        
        if( !retval->IsString() && isTmp ){
            printf("hash_remove: %s\n", ctx->id );
            ne_hash_remove( cs->parseCache, (void*)ctx->id );
            DestroyContext( ctx );
        }
    }
    
    return scope.Close( retval );
}

#define SetKeyPath(key, parent, parent_len, child, child_len)({ \
    char *ptr = key; \
    if( parent_len ){ \
        memcpy( ptr, parent, parent_len ); \
        ptr[parent_len] = '.'; \
        ptr += plen + 1; \
    } \
    memcpy( ptr, child, child_len ); \
    ptr[child_len] = 0; \
})

inline Handle<Value> ClearSilver::_setValue( HDF *hdf, Local<Object> obj, const char *const parentKey, Local<Array> refs )
{
    Handle<Value> retval = Undefined();
    Local<Array> props = obj->GetPropertyNames();
    uint32_t len = props->Length();
    
    if( len )
    {
        NEOERR *nerr = STATUS_OK;
        size_t plen = ( parentKey ) ? strlen( parentKey ) : 0;
        size_t klen = 0;
        char *ptr = NULL;
        Local<Value> key;
        Local<Value> val;
        uint32_t valType;
        char *estr = NULL;
        
        for( uint32_t idx = 0; !estr && idx < len; idx++ )
        {
            key = props->Get(idx);
            val = obj->Get(key);
            klen = key->ToString()->Utf8Length();
            valType = TypeOf( val );
            
            // printf(" idx: %u\n", idx);
            // printf("type: %s\n", TypeName( val ) );
            if( valType & isPrintable )
            {
                char name[plen+1+klen+1];
                SetKeyPath( name, parentKey, plen, *String::Utf8Value( key->ToString() ), klen );
                // Date milliseconds to iso8601
                if( valType & JS_TYPE_DATE_BIT ){
                    char iso8601[ISO8601_STRING_LEN];
                    MSEC_TO_ISO8601(iso8601,val->IntegerValue());
                    estr = CHECK_NEOERR( hdf_set_value( hdf, name, iso8601 ) );
                }
                else if( valType & JS_TYPE_BOOLEAN_BIT ){
                    estr = CHECK_NEOERR( hdf_set_int_value( hdf, name, ( val->BooleanValue() ) ? 1 : 0 ) );
                }
                else {
                    estr = CHECK_NEOERR( hdf_set_value( hdf, name, *String::Utf8Value( val->ToString() ) ) );
                }
            }
            else if( valType & isRecursive )
            {
                // check is circulative object
                bool isCircular = false;
                uint32_t nrefs = refs->Length();
                char name[plen+1+klen+1];
                
                SetKeyPath( name, parentKey, plen, *String::Utf8Value( key->ToString() ), klen );
                for( uint32_t i = 0; i < nrefs; i++ )
                {
                    if( ( isCircular = refs->Get(i)->StrictEquals(val) ) ){
                        estr = CHECK_NEOERR( hdf_set_value( hdf, name, "[Circular]" ) );
                        break;
                    }
                }
                if( !isCircular )
                {
                    refs->Set( nrefs, val );
                    if( !( retval = _setValue( hdf, val->ToObject(), name, refs ) )->IsUndefined() ){
                        break;
                    }
                }
            }
            else if( valType & isRemoval ){
                char name[plen+1+klen+1];
                SetKeyPath( name, parentKey, plen, *String::Utf8Value( key->ToString() ), klen );
                estr = CHECK_NEOERR( hdf_remove_tree( hdf, name ) );
            }
        }
        if( estr ){
            retval = ThrowException( Exception::Error( String::New( estr ) ) );
            free(estr);
        }
    }
    
    return retval;
}

Handle<Value> ClearSilver::setValue( const Arguments& argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    Handle<Value> retval = Null();
    const int argc = argv.Length();
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( 3 > argc || !argv[0]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "setValue( parser_id:String, key:[String|Undefined|Null], val:[String|Number|Date|Boolean|Array|Object] )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to setValue: parser not found" ) ) );
    }
    else
    {
        uint32_t valType = TypeOf( argv[2] );
        
        if( valType & isPrintable )
        {
            char *estr = NULL;
            
            if( !argv[1]->IsString() ){
                retval = ThrowException( Exception::TypeError( String::New( "setValue( parser_id:String, key:String, val:[String|Number|Date|Boolean] )" ) ) );
            }
            // Date milliseconds to iso8601
            else if( valType & JS_TYPE_DATE_BIT ){
                char iso8601[ISO8601_STRING_LEN];
                MSEC_TO_ISO8601( iso8601, argv[2]->IntegerValue() );
                estr = CHECK_NEOERR( hdf_set_value( ctx->hdf, *String::Utf8Value( argv[1] ), iso8601 ) );
            }
            else if( valType & JS_TYPE_BOOLEAN_BIT ){
                estr = CHECK_NEOERR( hdf_set_int_value( ctx->hdf, *String::Utf8Value( argv[1] ), ( argv[2]->BooleanValue() ) ? 1 : 0 ) );
            }
            else {
                estr = CHECK_NEOERR( hdf_set_value( ctx->hdf, *String::Utf8Value( argv[1] ), *String::Utf8Value( argv[2] ) ) );
            }
            
            if( estr ){
                retval = ThrowException( Exception::Error( String::New( estr ) ) );
                free(estr);
            }
        }
        else if( valType & isRecursive )
        {
            Local<Object> obj = argv[2]->ToObject();
            Local<Array> refs = Array::New();
            
            // set root object
            refs->Set( 0, obj );
            retval = _setValue( ctx->hdf, obj, ( !argv[1]->IsString() ) ? "" : *String::Utf8Value( argv[1] ), refs );
        }
    }
    
    return scope.Close( retval->IsNull() ? Handle<Value>() : retval );
}

Handle<Value> ClearSilver::getValue( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( 2 > argc || !argv[0]->IsString() || !argv[1]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "removeValue( parser_id:String, key:String )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to removeValue: parser not found" ) ) );
    }
    else
    {
        char *val = hdf_get_value( ctx->hdf, *String::Utf8Value( argv[1] ), NULL );
        if( val ){
            retval = String::New( val );
        }
    }
    
    return scope.Close( retval );
}

Handle<Value> ClearSilver::removeValue( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( 2 > argc || !argv[0]->IsString() || !argv[1]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "removeValue( parser_id:String, key:String )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to removeValue: parser not found" ) ) );
    }
    else
    {
        char *estr = NULL;
        
        if( ( estr = CHECK_NEOERR( hdf_remove_tree( ctx->hdf, *String::Utf8Value( argv[1] ) ) ) ) ){
            retval = ThrowException( Exception::ReferenceError( String::New( estr ) ) );
            free(estr);
        }
    }
    
    return scope.Close( retval );
}

Handle<Value> ClearSilver::dump( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( !argv[0]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "dump( parser_id:String )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to dump: parser not found" ) ) );
    }
    else
    {
        char *estr = NULL;
        STRING dump;
        
        string_init(&dump);
        if( ( estr = CHECK_NEOERR( hdf_dump_str(ctx->hdf, NULL, 2, &dump) ) ) ){
            retval = ThrowException( Exception::ReferenceError( String::New( estr ) ) );
            free(estr);
        }
        else {
            retval = String::New( dump.buf );
        }
        string_clear(&dump);
    }
    
    return scope.Close( retval );
}


Handle<Value> ClearSilver::render( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    bool callback = false;
    
    // invalid arguments
    if( !argv[0]->IsString() || ( 1 < argc && !( callback = argv[1]->IsFunction() ) ) ){
        retval = ThrowException( Exception::TypeError( String::New( "render( parser_id:String, [callback:Function] )" ) ) );
    }
    // find parser
    else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[0] ) ) ) ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to render: parser not found" ) ) );
    }
    else if( !ctx->csp ){
        retval = ThrowException( Exception::ReferenceError( String::New( "faild to render: parser_id does not parsed" ) ) );
    }
    // render async
    else if( callback )
    {
        Baton_t *baton = new Baton_t();
        baton->ctx = (void*)ctx;
        baton->data = NULL;
        // detouch from GC
        baton->callback = Persistent<Function>::New( Local<Function>::Cast( argv[1] ) );
        cs->Ref();
        baton->req = eio_custom( renderBeginEIO, EIO_PRI_DEFAULT, renderEndEIO, baton );
        ev_ref(EV_DEFAULT_UC);
    }
    // parse sync
    else
    {
        char *estr = NULL;
        STRING page;
        
        string_init(&page);
        
        // render
        if( ( estr = CHECK_NEOERR( cs_render( ctx->csp, &page, callbackRender ) ) ) ){
            retval = ThrowException( Exception::Error( String::New( estr ) ) );
            free(estr);
        }
        else {
            retval = String::New( page.buf );
        }
        string_clear(&page);
    }
    
    return scope.Close( retval );
}


int ClearSilver::renderBeginEIO( eio_req *req )
{
    Baton_t *baton = static_cast<Baton_t*>( req->data );
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    
    if( pthread_mutex_lock( &ctx->mutex ) ){
        baton->nerr = nerr_raise( NERR_SYSTEM, "Mutex lock failed: %s", strerror(errno) );
    }
    else
    {
        STRING page;
        
        string_init(&page);
        if( STATUS_OK == ( baton->nerr = cs_render( ctx->csp, &page, callbackRender ) ) ){
            baton->data = malloc( page.len+1 );
            memcpy( (void*)baton->data, (void*)page.buf, page.len );
            ((char*)baton->data)[page.len] = 0;
        }
        string_clear(&page);
        errno = 0;
        if( pthread_mutex_unlock( &ctx->mutex ) )
        {
            free( baton->data );
            if( STATUS_OK != baton->nerr ){
                free(baton->nerr);
            }
            baton->nerr = nerr_raise( NERR_LOCK, "Mutex unlock failed: %s", strerror(errno) );
        }
    }
    
    return 0;
}

int ClearSilver::renderEndEIO( eio_req *req )
{
    HandleScope scope;
    Baton_t *baton = static_cast<Baton_t*>(req->data);
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    Handle<Primitive> t = Undefined();
    Local<Value> argv[] = {
        reinterpret_cast<Local<Value>&>(t),
        reinterpret_cast<Local<Value>&>(t)
    };
    
    ev_unref(EV_DEFAULT_UC);
    ctx->cs->Unref();
    
    if( STATUS_OK == baton->nerr ){
        argv[1] = String::New( (char*)baton->data );
        free( baton->data );
    }
    else {
        const char *errstr = CHECK_NEOERR( baton->nerr );
        baton->nerr = STATUS_OK;
        argv[0] = Exception::Error( String::New( errstr ) );
        free( (void*)errstr );
    }
    
    TryCatch try_catch;
    // call js function by callback function context
    baton->callback->Call( baton->callback, 2, argv );
    if( try_catch.HasCaught() ){
        FatalException(try_catch);
    }
    // remove callback
    baton->callback.Dispose();
    delete baton;
    
    eio_cancel(req);
    
    return 0;
}

NEOERR *ClearSilver::callbackRender( void *ctx, char *str )
{
    return ( str ) ? string_append( (STRING*)ctx, str ) : STATUS_OK;
}

// call from main or other thread
NEOERR *ClearSilver::hookFileload( void *context, HDF *hdf, const char *filepath, char **inject )
{
    NEOERR *nerr = STATUS_OK;
    //HandleScope scope;
    ParseCtx_t *ctx = (ParseCtx_t*)context;
    HDF *paths = hdf_get_child( hdf, "Config.loadpaths" );
    char *resolve = NULL;
    char *errstr = NULL;
    
    if( !paths )
    {
        if( !( resolve = realpath( filepath, NULL ) ) )
        {
            errstr = strerror(errno);
            switch (errno)
            {
                case EINVAL:
                case ENAMETOOLONG:
                    nerr = nerr_raise( NERR_ASSERT, "%s", errstr );
                break;
                case ENOENT:
                case ENOTDIR:
                    nerr = nerr_raise( NERR_NOT_FOUND, "%s", errstr );
                break;
                /* case EACCES:    // Read or search permission was denied for a component of the path prefix.
                   case EIO:       // An I/O error occurred while reading from the file system.
                   case ELOOP:     // Too many symbolic links were encountered in translating the pathname. */
                default:
                    nerr = nerr_raise( NERR_SYSTEM, "%s", errstr );
            }
        }
    }
    else
    {
        char *loadpath = NULL;
        char *fullpath = NULL;
        
        do
        {
            if( ( loadpath = hdf_obj_value( paths ) ) )
            {
                // critical errror
                if( ( -1 == asprintf( &fullpath, "%s/%s", loadpath, filepath ) ) ){
                    nerr = nerr_raise( ( errno == ENOMEM ) ? NERR_NOMEM : NERR_ASSERT, "%s", strerror(errno) );
                    break;
                }
                // check errno
                else if( !( resolve = realpath( fullpath, NULL ) ) )
                {
                    errstr = strerror(errno);
                    switch (errno)
                    {
                        case ENOENT:
                        case ENOTDIR:
                            // continue
                            // nerr = nerr_raise( NERR_NOT_FOUND, "%s", strerror(errno) );
                        break;
                        case EINVAL:
                        case ENAMETOOLONG:
                            nerr = nerr_raise( NERR_ASSERT, "%s", errstr );
                        break;
                        default:
                            nerr = nerr_raise( NERR_SYSTEM, "%s", errstr );
                    }
                }
                // found but not secure
                else if( 0 != strncmp( loadpath, resolve, strlen( loadpath ) ) ){
                    nerr = nerr_raise( NERR_SYSTEM, "%s", strerror(EACCES) );
                }
                free( fullpath );
                fullpath = NULL;
                
                if( resolve || nerr != STATUS_OK ){
                    break;
                }
            }
        
        }while( ( paths = hdf_obj_next( paths ) ) );
        
        if( nerr != STATUS_OK ){
            free(resolve);
            resolve = NULL;
        }
    }
    
    if( resolve )
    {
        KeyVal_t *cache = NULL;
        
        // cache lock
        if( pthread_mutex_lock( &ctx->cs->mutex ) ){
            nerr = nerr_raise( NERR_LOCK, "Mutex lock failed: %s", strerror(errno) );
        }
        else {
            cache = (KeyVal_t*)ne_hash_lookup( ctx->cs->fileCache, (void*)resolve );
        }
        
        // load file if no cached
        if( !cache )
        {
            if( !( cache = (KeyVal_t*)calloc( 1, sizeof( KeyVal_t ) ) ) ){
                // critical
                nerr = nerr_raise( NERR_SYSTEM, "%s", strerror(errno) );
            }
            else if( -1 == asprintf( (char**)&cache->id, resolve ) ){
                // critical
                nerr = nerr_raise( NERR_SYSTEM, "%s", strerror(errno) );
            }
            else if( STATUS_OK != ( nerr = ne_load_file( resolve, (char**)&cache->data ) ) ||
                     STATUS_OK != ( nerr = ne_hash_insert( ctx->cs->fileCache, cache->id, (void*)cache ) ) )
            {
                free(cache->id);
                free(cache);
                cache = NULL;
            }
            else {
                free(resolve);
            }
        }
        
        if( STATUS_OK == nerr )
        {
            char *ext = rindex( (char*)cache->id, '.' );
        
            // is HDF
            if( ext && !strcmp( ext, ".hdf" ) ){
                nerr = hdf_read_string( hdf, (char*)cache->data );
                asprintf( inject, "" );
            }
            // is text
            else if( -1 == asprintf( inject, "%s", (char*)cache->data ) ){
                // critical
                nerr = nerr_raise( NERR_SYSTEM, "%s", strerror(errno) );
            }
            
            // remove cache
            if( STATUS_OK != nerr ){
                ne_hash_remove( ctx->cs->fileCache, cache->id );
                free(cache->id);
                free(cache);
                cache = NULL;
            }
        }
    }
    
    if( !inject && -1 == asprintf( inject, "[include] failed to include %s\n", strerror(ENOENT) ) ){
        // critical
        nerr = nerr_raise( NERR_SYSTEM, "%s", strerror(errno) );
    }

    return nerr_pass(nerr);
}

/*

int ClearSilver::parseStringBeginEIO( eio_req *req )
{
    Baton_t *baton = static_cast<Baton_t*>( req->data );
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    char *timestamp = NULL;
    
    CurrentTimestamp( &timestamp );
    printf("  pthread_mutex_lock: %s:%s\n", ctx->id, timestamp );
    if( pthread_mutex_lock( &ctx->mutex ) ){
        baton->nerr = nerr_raise( NERR_SYSTEM, "Mutex lock failed: %s", strerror(errno) );
    }
    else
    {
        printf("pthread_mutex_locked: %s:%s\n", ctx->id, timestamp );
        if( STATUS_OK != ( baton->nerr = cs_parse_string( ctx->csp, (char*)ctx->tmpl, ctx->len ) ) )
        {
            // printf("ERR! %d,%d\n", NERR_SYSTEM, baton->nerr->error );
            if( baton->nerr->error == NERR_NOT_FOUND ){
                free(baton->nerr);
                baton->nerr = STATUS_OK;
            }
        }
        
        errno = 0;
        if( pthread_mutex_unlock( &ctx->mutex ) )
        {
            if( STATUS_OK != baton->nerr ){
                free(baton->nerr);
            }
            baton->nerr = nerr_raise( NERR_LOCK, "Mutex unlock failed: %s", strerror(errno) );
        }
    }
    
    return 0;
}

int ClearSilver::parseStringEndEIO( eio_req *req )
{
    HandleScope scope;
    Baton_t *baton = static_cast<Baton_t*>(req->data);
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    Handle<Primitive> t = Undefined();
    Local<Value> argv[] = {
        reinterpret_cast<Local<Value>&>(t),
        String::New( ctx->id )
    };
    
    ev_unref(EV_DEFAULT_UC);
    ctx->cs->Unref();
    
    if( STATUS_OK != baton->nerr ){
        const char *errstr = CHECK_NEOERR( baton->nerr );
        baton->nerr = STATUS_OK;
        argv[0] = Exception::Error( String::New( errstr ) );
        free( (void*)errstr );
    }
    else {
        ctx->delWhenFailed = false;
    }
    
    TryCatch try_catch;
    // call js function by callback function context
    baton->callback->Call( baton->callback, 2, argv );
    // destroy parser
    if( ctx->delWhenFailed ){
        // !!!: add call destroyParser
    }
    if( try_catch.HasCaught() ){
        FatalException(try_catch);
    }
    // remove callback
    baton->callback.Dispose();
    delete baton;

    return 0;
}
*/


/*Handle<Value> ClearSilver::parseString( const Arguments &argv )
{
    HandleScope scope;
    ClearSilver *cs = ObjectUnwrap( ClearSilver, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    unsigned int callback = 0;
    ParseCtx_t *ctx = NULL;
    
    // invalid arguments
    if( !argv[0]->IsString() ){
        retval = ThrowException( Exception::TypeError( String::New( "parseString( template:String, [parser_id:String], [callback:Function] )" ) ) );
    }
    // arguments has parser_id and callback
    else if( 2 < argc )
    {
        // invalid arguments
        if( !argv[1]->IsString() || !argv[2]->IsFunction() ){
            retval = ThrowException( Exception::TypeError( String::New( "parseString( template:String, [parser_id:String], [callback:Function] )" ) ) );
        }
        // find parser
        else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[1] ) ) ) ){
            retval = ThrowException( Exception::ReferenceError( String::New( "faild to parseString: parser not found" ) ) );
        }
        else {
            callback = 2;
        }
    }
    // arguments has parser_id or callback
    else if( 1 < argc )
    {
        // invalid arguments
        if( !argv[1]->IsString() && !argv[1]->IsFunction() ){
            retval = ThrowException( Exception::TypeError( String::New( "parseString( template:String, [parser_id:String], [callback:Function] )" ) ) );
        }
        // is parser_id
        else if( argv[1]->IsString() )
        {
            // find parser
            if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( argv[1] ) ) ) ){
                retval = ThrowException( Exception::ReferenceError( String::New( "faild to parseString: parser not found" ) ) );
            }
        }
        // is callback
        else {
            callback = 1;
        }
    }

    if( retval->IsUndefined() )
    {
        // create parser
        if( !ctx )
        {
            // create parser
            Handle<Value> parser_id = cs->_createParser( NULL, true );
            // exception
            if( !parser_id->IsString() ){
                retval = parser_id;
            }
            // unknown error
            else if( !( ctx = (ParseCtx_t*)ne_hash_lookup( cs->parseCache, (void*)*String::Utf8Value( parser_id ) ) ) ){
                retval = ThrowException( Exception::ReferenceError( String::New( "faild to parseString: parser not found" ) ) );
            }
        }
        
        if( ctx )
        {
            // already compiled
            
            if( ctx->tmpl )
            {
                // fprintf( stdout, "no parse: %s\n", ctx->id );
                // parse async
                if( callback )
                {
                    Handle<Primitive> t = Undefined();
                    Local<Function> cb = Local<Function>::Cast( argv[callback] );
                    Local<Value> args[] = {
                        reinterpret_cast<Local<Value>&>(t),
                        String::New( ctx->id )
                    };
                    cb->Call( cb, 2, args );
                }
                // parse sync
                else {
                    retval = String::New( ctx->id );
                }
            }
            else
            {
                // copy template string
                if( !ctx->tmpl && -1 == ( ctx->len = asprintf( (char**)&ctx->tmpl, "%s", *String::Utf8Value( argv[0] ) ) ) ){
                    retval = ThrowException( Exception::Error( String::New( strerror(errno) ) ) );
                    ctx->tmpl = NULL;
                    ctx->len = 0;
                    // !!!: add call destroyParser
                    // code...
                }
                // parse async
                else if( callback )
                {
                    Baton_t *baton = new Baton_t();
                    baton->ctx = (void*)ctx;
                    // detouch from GC
                    baton->callback = Persistent<Function>::New( Local<Function>::Cast( argv[callback] ) );
                    cs->Ref();
                    eio_custom( parseStringBeginEIO, EIO_PRI_DEFAULT, parseStringEndEIO, baton );
                    ev_ref(EV_DEFAULT_UC);
                }
                // parse sync
                else
                {
                    char *estr = NULL;
                    
                    // failed
                    if( ( estr = CHECK_NEOERR( cs_parse_string( ctx->csp, (char*)ctx->tmpl, ctx->len ) ) ) )
                    {
                        retval = ThrowException( Exception::Error( String::New( estr ) ) );
                        free(estr);
                        if( ctx->delWhenFailed ){
                            free((void*)ctx->tmpl);
                            ctx->tmpl = NULL;
                            ctx->len = 0;
                            // !!!: add call destroyParser if dynamic create parser
                        }
                    }
                    else {
                        ctx->delWhenFailed = false;
                        retval = String::New( ctx->id );
                    }
                }
            //}
        }
    }
    
    return scope.Close( retval );
}
*/

void ClearSilver::Initialize( Handle<Object> target )
{
    HandleScope scope;
    Local<FunctionTemplate> t = FunctionTemplate::New( New );
    
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName( String::NewSymbol("ClearSilver") );
    NODE_SET_PROTOTYPE_METHOD( t, "createParser", createParser );
    NODE_SET_PROTOTYPE_METHOD( t, "parseString", parseString );
    NODE_SET_PROTOTYPE_METHOD( t, "removeParser", removeParser );
    NODE_SET_PROTOTYPE_METHOD( t, "render", render );
    NODE_SET_PROTOTYPE_METHOD( t, "setValue", setValue );
    NODE_SET_PROTOTYPE_METHOD( t, "getValue", getValue );
    NODE_SET_PROTOTYPE_METHOD( t, "removeValue", removeValue );
    NODE_SET_PROTOTYPE_METHOD( t, "dump", dump );
    target->Set( String::NewSymbol("ClearSilver"), t->GetFunction() );
}

extern "C" {
    static void init( Handle<Object> target ){
        HandleScope scope;
        ClearSilver::Initialize( target );
    }
    NODE_MODULE( clearsilver, init );
};
