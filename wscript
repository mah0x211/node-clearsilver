import Options
from os import unlink, symlink, popen
from os.path import exists

srcdir = '.'
blddir = 'build'
VERSION = '0.5.0'

def set_options(opt):
	opt.tool_options('compiler_cxx')
	opt.tool_options('compiler_cc')

def configure(conf):
	conf.check_tool('compiler_cxx')
	if not conf.env.CXX: conf.fatal('c++ compiler not found')
	conf.check_tool('compiler_cc')
	if not conf.env.CC: conf.fatal('c compiler not found')
	conf.check_tool('node_addon')
	
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