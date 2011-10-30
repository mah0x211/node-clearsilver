import Options
import Environment
import sys, os, shutil, glob
from os import unlink, symlink, popen
from os.path import join, dirname, abspath, normpath

srcdir = '.'
blddir = 'build'
VERSION = '0.5.0'

def set_options(opt):
	opt.tool_options('compiler_cxx')
	opt.tool_options('compiler_cc')
	opt.tool_options('misc')
	
	opt.add_option( '--clearsilver-includes'
		, action='store'
		, type='string'
		, default=False
		, help='Directory containing libev header files'
		, dest='clearsilver_includes'
		)
	
	opt.add_option( '--clearsilver'
		, action='store'
		, type='string'
		, default=False
		, help='Link to a shared clearsilver libraries'
		, dest='clearsilver'
		)

def configure(conf):
	conf.check_tool('compiler_cxx')
	if not conf.env.CXX: conf.fatal('c++ compiler not found')
	conf.check_tool('compiler_cc')
	if not conf.env.CC: conf.fatal('c compiler not found')
	conf.check_tool('node_addon')
	
	o = Options.options
	
	if o.clearsilver_includes:
	    conf.env.append_value("CPPFLAGS", '-I%s' % o.clearsilver_includes)
	
	if o.clearsilver:
	    conf.env.append_value("LINKFLAGS", '-L%s' % o.clearsilver)
	
	# print conf.env
	
	# check ClearSilver libs
	conf.check_cc( lib='neo_cs', mandatory=True )
	conf.check_cc( lib='neo_utl', mandatory=True )
	conf.check_cc( lib='neo_cgi', mandatory=True )

def build(bld):
	print 'build'
	obj = bld.new_task_gen('cxx', 'shlib', 'node_addon')
	obj.target = 'clearsilver_binding'
	obj.source = './src/clearsilver.cc'
	obj.includes = ['.']
	obj.lib = ['neo_cs','neo_cgi','neo_utl']

def shutdown(ctx):
	pass
