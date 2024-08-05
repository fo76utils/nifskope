#!/usr/bin/python

# gen_niflib.py
#
# This script generates C++ code for Niflib.
#
# --------------------------------------------------------------------------
# Command line options
#
# -p /path/to/niflib : specifies the path where niflib can be found 
#
# -b : enable bootstrap mode (generates templates)
# 
# -i : do NOT generate implmentation; place all code in defines.h
#
# -a : generate accessors for data in classes
#
# -n <block>: generate only files which match the specified name
#
# --------------------------------------------------------------------------
# ***** BEGIN LICENSE BLOCK *****
#
# Copyright (c) 2005, NIF File Format Library and Tools
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#
#    * Neither the name of the NIF File Format Library and Tools
#      project nor the names of its contributors may be used to endorse
#      or promote products derived from this software without specific
#      prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# ***** END LICENSE BLOCK *****
# --------------------------------------------------------------------------

from nifxml import *
from distutils.dir_util import mkpath
import os
import hashlib
import itertools

#
# global data
#

ROOT_DIR = ".."
BOOTSTRAP = False
GENIMPL = True
GENACCESSORS = False
GENBLOCKS = []
GENALLFILES = True

prev = ""
for i in sys.argv:
    if prev == "-p":
        ROOT_DIR = i
    elif i == "-b":
        BOOTSTRAP = True
    elif i == "-i":
        GENIMPL = False
    elif i == "-a":
        GENACCESSORS = True
    elif prev == "-n":
        GENBLOCKS.append(i)
        GENALLFILES = False
        
    prev = i

    
# Fix known manual update attributes. For now hard code here.
block_types["NiKeyframeData"].find_member("Num Rotation Keys").is_manual_update = True
#block_types["NiTriStripsData"].find_member("Num Triangles").is_manual_update = True

#
# Function to extract custom code from existing file
#
def ExtractCustomCode( file_name ):
    custom_lines = {}
    custom_lines['MISC'] = []
    custom_lines['FILE HEAD'] = []
    custom_lines['FILE FOOT'] = []
    custom_lines['PRE-READ'] = []
    custom_lines['POST-READ'] = []
    custom_lines['PRE-WRITE'] = []
    custom_lines['POST-WRITE'] = []
    custom_lines['PRE-STRING'] = []
    custom_lines['POST-STRING'] = []
    custom_lines['PRE-FIXLINKS'] = []
    custom_lines['POST-FIXLINKS'] = []
    custom_lines['CONSTRUCTOR'] = []
    custom_lines['DESTRUCTOR'] = []
    
    if os.path.isfile( file_name ) == False:
        custom_lines['MISC'].append( "\n" )
        custom_lines['FILE HEAD'].append( "\n" )
        custom_lines['FILE FOOT'].append( "\n" )
        custom_lines['PRE-READ'].append( "\n" )
        custom_lines['POST-READ'].append( "\n" )
        custom_lines['PRE-WRITE'].append( "\n" )
        custom_lines['POST-WRITE'].append( "\n" )
        custom_lines['PRE-STRING'].append( "\n" )
        custom_lines['POST-STRING'].append( "\n" )
        custom_lines['PRE-FIXLINKS'].append( "\n" )
        custom_lines['POST-FIXLINKS'].append( "\n" )
        custom_lines['CONSTRUCTOR'].append( "\n" )
        custom_lines['DESTRUCTOR'].append( "\n" )
        return custom_lines
    
    f = file( file_name, 'r' )
    lines = f.readlines()
    f.close()
   
    custom_flag = False
    custom_name = ""
    
    for l in lines:
        if custom_flag == True:
            if l.find( '//--END CUSTOM CODE--//' ) != -1:
                custom_flag = False
            else:
                if not custom_lines[custom_name]:
                    custom_lines[custom_name] = [l]
                else:
                    custom_lines[custom_name].append(l)
        if l.find( '//--BEGIN MISC CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'MISC'
        elif l.find( '//--BEGIN FILE HEAD CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'FILE HEAD'
        elif l.find( '//--BEGIN FILE FOOT CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'FILE FOOT'
        elif l.find( '//--BEGIN PRE-READ CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'PRE-READ'
        elif l.find( '//--BEGIN POST-READ CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'POST-READ'
        elif l.find( '//--BEGIN PRE-WRITE CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'PRE-WRITE'
        elif l.find( '//--BEGIN POST-WRITE CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'POST-WRITE'
        elif l.find( '//--BEGIN PRE-STRING CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'PRE-STRING'
        elif l.find( '//--BEGIN POST-STRING CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'POST-STRING'
        elif l.find( '//--BEGIN PRE-FIXLINKS CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'PRE-FIXLINKS'
        elif l.find( '//--BEGIN POST-FIXLINKS CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'POST-FIXLINKS'
        elif l.find( '//--BEGIN CONSTRUCTOR CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'CONSTRUCTOR'
        elif l.find( '//--BEGIN DESTRUCTOR CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'DESTRUCTOR'
        elif l.find( '//--BEGIN INCLUDE CUSTOM CODE--//' ) != -1:
            custom_flag = True
            custom_name = 'INCLUDE'
    
    return custom_lines

#
# Function to compare two files
#

def OverwriteIfChanged( original_file, candidate_file ):
    files_differ = False

    if os.path.isfile( original_file ):
        f1 = file( original_file, 'r' )
        f2 = file( candidate_file, 'r' )

        s1 = f1.read()
        s2 = f2.read()

        f1.close()
        f2.close()
        
        if s1 != s2:
            files_differ = True
            #remove original file
            os.unlink( original_file )
    else:
        files_differ = True

    if files_differ:
        #Files differ, so overwrite original with candidate
        os.rename( candidate_file, original_file )
   
#
# generate compound code
#

mkpath(os.path.join(ROOT_DIR, "include/obj"))
mkpath(os.path.join(ROOT_DIR, "include/gen"))

mkpath(os.path.join(ROOT_DIR, "src/obj"))
mkpath(os.path.join(ROOT_DIR, "src/gen"))

for n in compound_names:
    x = compound_types[n]
    
    # skip natively implemented types
    if x.niflibtype: continue
    if n[:3] == 'ns ': continue
    
    if not GENALLFILES and not x.cname in GENBLOCKS:
            continue
        
    #Get existing custom code
    file_name = ROOT_DIR + '/include/gen/' + x.cname + '.h'
    custom_lines = ExtractCustomCode( file_name );

    h = CFile(file_name, 'w')  
    h.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    h.code( 'All rights reserved.  Please see niflib.h for license. */' )
    h.code()
    h.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
    h.code()
    h.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
    h.code()
    h.code( '#ifndef _' + x.cname.upper() + '_H_' )
    h.code( '#define _' + x.cname.upper() + '_H_' )
    h.code()
    h.code( '#include "../NIF_IO.h"' )
    if n in ["Header", "Footer"]:
        h.code( '#include "../obj/NiObject.h"' )
    h.code( x.code_include_h() )
    h.write( "namespace Niflib {\n" )
    h.code( x.code_fwd_decl() )
    h.code()
    # header
    h.comment(x.description)
    hdr = "struct %s"%x.cname
    if x.template: hdr = "template <class T >\n%s"%hdr
    hdr += " {"
    h.code(hdr)
    
    #constructor/destructor/assignment
    if not x.template:
        h.code( '/*! Default Constructor */' )
        h.code( "NIFLIB_API %s();"%x.cname )
        h.code( '/*! Default Destructor */' )
        h.code( "NIFLIB_API ~%s();"%x.cname )
        h.code( '/*! Copy Constructor */' )
        h.code( 'NIFLIB_API %s( const %s & src );'%(x.cname, x.cname) )
        h.code( '/*! Copy Operator */' )
        h.code( 'NIFLIB_API %s & operator=( const %s & src );'%(x.cname, x.cname) )



    # declaration
    h.declare(x)

    # header and footer functions
    if n  == "Header":
        h.code( 'NIFLIB_HIDDEN NifInfo Read( istream& in );' )
        h.code( 'NIFLIB_HIDDEN void Write( ostream& out, const NifInfo & info = NifInfo() ) const;' )
        h.code( 'NIFLIB_HIDDEN string asString( bool verbose = false ) const;' )
    
    if n == "Footer":
        h.code( 'NIFLIB_HIDDEN void Read( istream& in, list<unsigned int> & link_stack, const NifInfo & info );' )
        h.code( 'NIFLIB_HIDDEN void Write( ostream& out, const map<NiObjectRef,unsigned int> & link_map, list<NiObject *> & missing_link_stack, const NifInfo & info ) const;' )
        h.code( 'NIFLIB_HIDDEN string asString( bool verbose = false ) const;' )

    h.code( '//--BEGIN MISC CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['MISC']:
        h.write(l);
        
    h.code( '//--END CUSTOM CODE--//' )

    # done
    h.code("};")
    h.code()
    h.write( "}\n" )
    h.code( '#endif' )
    h.close()

    if not x.template:
        #Get existing custom code
        file_name = ROOT_DIR + '/src/gen/' + x.cname + '.cpp'
        custom_lines = ExtractCustomCode( file_name );

        cpp = CFile(file_name, 'w')
        cpp.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
        cpp.code( 'All rights reserved.  Please see niflib.h for license. */' )
        cpp.code()
        cpp.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
        cpp.code()
        cpp.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
        cpp.code()
        cpp.code( x.code_include_cpp( True, "../../include/gen/", "../../include/obj/" ) )
        cpp.code( "using namespace Niflib;" )
        cpp.code()
        cpp.code( '//Constructor' )
        
        # constructor
        x_code_construct = x.code_construct()
        #if x_code_construct:
        cpp.code("%s::%s()"%(x.cname,x.cname) + x_code_construct + " {};")
        cpp.code()

        cpp.code('//Copy Constructor')
        cpp.code( '%s::%s( const %s & src ) {'%(x.cname,x.cname,x.cname) )
        cpp.code( '*this = src;' )
        cpp.code('};')
        cpp.code()

        cpp.code('//Copy Operator')
        cpp.code( '%s & %s::operator=( const %s & src ) {'%(x.cname,x.cname,x.cname) )
        for m in x.members:
            if not m.is_duplicate:
                cpp.code('this->%s = src.%s;'%(m.cname, m.cname) )
        cpp.code('return *this;')
        cpp.code('};')
        cpp.code()

        cpp.code( '//Destructor' )
        
        # destructor
        cpp.code("%s::~%s()"%(x.cname,x.cname) + " {};")

        # header and footer functions
        if n  == "Header":
            cpp.code( 'NifInfo ' + x.cname + '::Read( istream& in ) {' )
            cpp.code( '//Declare NifInfo structure' )
            cpp.code( 'NifInfo info;' )
            cpp.code()
            cpp.stream(x, ACTION_READ)
            cpp.code()
            cpp.code( '//Copy info.version to local version var.' )
            cpp.code( 'version = info.version;' )
            cpp.code()
            cpp.code( '//Fill out and return NifInfo structure.' )
            cpp.code( 'info.userVersion = userVersion;' )
            cpp.code( 'info.userVersion2 = userVersion2;' )
            cpp.code( 'info.endian = EndianType(endianType);' )
            cpp.code( 'info.creator = exportInfo.creator.str;' )
            cpp.code( 'info.exportInfo1 = exportInfo.exportInfo1.str;' )
            cpp.code( 'info.exportInfo2 = exportInfo.exportInfo2.str;' )
            cpp.code()
            cpp.code( 'return info;' )
            cpp.code()
            cpp.code( '}' )
            cpp.code()
            cpp.code( 'void ' + x.cname + '::Write( ostream& out, const NifInfo & info ) const {' )
            cpp.stream(x, ACTION_WRITE)
            cpp.code( '}' )
            cpp.code()
            cpp.code( 'string ' + x.cname + '::asString( bool verbose ) const {' )
            cpp.stream(x, ACTION_OUT)
            cpp.code( '}' )
        
        if n == "Footer":
            cpp.code()
            cpp.code( 'void ' + x.cname + '::Read( istream& in, list<unsigned int> & link_stack, const NifInfo & info ) {' )
            cpp.stream(x, ACTION_READ)
            cpp.code( '}' )
            cpp.code()
            cpp.code( 'void ' + x.cname + '::Write( ostream& out, const map<NiObjectRef,unsigned int> & link_map, list<NiObject *> & missing_link_stack, const NifInfo & info ) const {' )
            cpp.stream(x, ACTION_WRITE)
            cpp.code( '}' )
            cpp.code()
            cpp.code( 'string ' + x.cname + '::asString( bool verbose ) const {' )
            cpp.stream(x, ACTION_OUT)
            cpp.code( '}' )

        cpp.code()
        cpp.code( '//--BEGIN MISC CUSTOM CODE--//' )

        #Preserve Custom code from before
        for l in custom_lines['MISC']:
            cpp.write(l);
        
        cpp.code( '//--END CUSTOM CODE--//' )

        cpp.close()

    # Write out Public Enumeration header Enumerations
if GENALLFILES:
    out = CFile(ROOT_DIR + '/include/gen/enums.h', 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code('#ifndef _NIF_ENUMS_H_')
    out.code('#define _NIF_ENUMS_H_')
    out.code()
    out.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
    out.code()
    out.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
    out.code()
    out.code( '#include <iostream>' )
    out.code( 'using namespace std;' )
    out.code()
    out.write('namespace Niflib {\n')
    out.code()
    for n, x in itertools.chain(enum_types.iteritems(), flag_types.iteritems()):
      if x.options:
        if x.description:
          out.comment(x.description)
        out.code('enum %s {'%(x.cname))
        for o in x.options:
          out.code('%s = %s, /*!< %s */'%(o.cname, o.value, o.description))
        out.code('};')
        out.code()
        out.code('ostream & operator<<( ostream & out, %s const & val );'%x.cname)
        out.code()

    out.write('}\n')
    out.code('#endif')
    out.close()

    # Write out Internal Enumeration header (NifStream functions)
if GENALLFILES:
    out = CFile(ROOT_DIR + '/include/gen/enums_intl.h', 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code()
    out.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
    out.code()
    out.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
    out.code()
    out.code('#ifndef _NIF_ENUMS_INTL_H_')
    out.code('#define _NIF_ENUMS_INTL_H_')
    out.code()
    out.code( '#include <iostream>' )
    out.code( 'using namespace std;' )
    out.code()
    out.code('#include "../nif_basic_types.h"')
    out.code()
    out.write('namespace Niflib {\n')
    out.code()
    for n, x in itertools.chain(enum_types.iteritems(), flag_types.iteritems()):
      if x.options:
        if x.description:
            out.code()
            out.code( '//---' + x.cname + '---//')
            out.code()
        out.code('void NifStream( %s & val, istream& in, const NifInfo & info = NifInfo() );'%x.cname)
        out.code('void NifStream( %s const & val, ostream& out, const NifInfo & info = NifInfo() );'%x.cname)
        out.code()

    out.write('}\n')
    out.code('#endif')
    out.close()

    #Write out Enumeration Implementation
if GENALLFILES:
    out = CFile(ROOT_DIR + '/src/gen/enums.cpp', 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code()
    out.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
    out.code()
    out.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
    out.code()
    out.code('#include <string>')
    out.code('#include <iostream>')
    out.code('#include "../../include/NIF_IO.h"')
    out.code('#include "../../include/gen/enums.h"')
    out.code('#include "../../include/gen/enums_intl.h"')
    out.code()
    out.code('using namespace std;')
    out.code()
    out.write('namespace Niflib {\n')
    out.code()

    out.code()
    for n, x in itertools.chain(enum_types.iteritems(), flag_types.iteritems()):
      if x.options:
        out.code()
        out.code('//--' + x.cname + '--//')
        out.code()
        out.code('void NifStream( %s & val, istream& in, const NifInfo & info ) {'%(x.cname))
        out.code('%s temp;'%(x.storage))
        out.code('NifStream( temp, in, info );')
        out.code('val = %s(temp);'%(x.cname))
        out.code('}')
        out.code()
        out.code('void NifStream( %s const & val, ostream& out, const NifInfo & info ) {'%(x.cname))
        out.code('NifStream( (%s)(val), out, info );'%(x.storage))
        out.code('}')
        out.code()
        out.code('ostream & operator<<( ostream & out, %s const & val ) { '%(x.cname))
        out.code('switch ( val ) {')
        for o in x.options:
          out.code('case %s: return out << "%s";'%(o.cname, o.name))
        out.code('default: return out << "Invalid Value! - " << (unsigned int)(val);')
        out.code('}')
        out.code('}')
        out.code()
        
    out.write('}\n')
    out.close()

    #
    # NiObject Registration Function
    #
    out = CFile(ROOT_DIR + '/src/gen/register.cpp', 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code()
    out.code( '//---THIS FILE WAS AUTOMATICALLY GENERATED.  DO NOT EDIT---//' )
    out.code()
    out.code( '//To change this file, alter the niftools/docsys/gen_niflib.py Python script.' )
    out.code()
    out.code( '#include "../../include/ObjectRegistry.h"' )
    for n in block_names:
        x = block_types[n]
        out.code( '#include "../../include/obj/' + x.cname + '.h"' )
    out.code()
    out.code( 'namespace Niflib {' )
    out.code( 'void RegisterObjects() {' )
    out.code()
    for n in block_names:
        x = block_types[n]
        out.code( 'ObjectRegistry::RegisterObject( "' + x.cname + '", ' + x.cname + '::Create );' )
    out.code()
    out.code( '}' )
    out.code( '}' )
    out.close()
    

#
# NiObject Files
#
for n in block_names:
    x = block_types[n]
    x_define_name = define_name(x.cname)

    if not GENALLFILES and not x.cname in GENBLOCKS:
        continue
    
    #
    # NiObject Header File
    #

    #Get existing custom code
    file_name = ROOT_DIR + '/include/obj/' + x.cname + '.h'
    custom_lines = ExtractCustomCode( file_name );

    #output new file
    out = CFile(file_name, 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code()
    out.code( '//-----------------------------------NOTICE----------------------------------//' )
    out.code( '// Some of this file is automatically filled in by a Python script.  Only    //' )
    out.code( '// add custom code in the designated areas or it will be overwritten during  //' )
    out.code( '// the next update.                                                          //' )
    out.code( '//-----------------------------------NOTICE----------------------------------//' )
    out.code()   
    out.code( '#ifndef _' + x.cname.upper() + '_H_' )
    out.code( '#define _' + x.cname.upper() + '_H_' )
    out.code()
    out.code( '//--BEGIN FILE HEAD CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['FILE HEAD']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.code( x.code_include_h() )
    out.write( "namespace Niflib {\n" )
    if not x.inherit:
        out.code( 'using namespace std;' )
    out.code( x.code_fwd_decl() )
    out.code( 'class ' + x.cname + ';' )
    out.code( 'typedef Ref<' + x.cname + '> ' + x.cname + 'Ref;' )
    out.code()
    out.comment( x.description )
    if x.inherit:
        out.code( 'class ' + x.cname + ' : public ' + x.inherit.cname + ' {' )
    else:
        out.code( 'class ' + x.cname + ' : public RefObject {' )
    out.code( 'public:' )
    out.code( '/*! Constructor */' )
    out.code( 'NIFLIB_API ' + x.cname + '();' )
    out.code()
    out.code( '/*! Destructor */' )
    out.code( 'NIFLIB_API virtual ~' + x.cname + '();' )
    out.code()
    out.code( '/*!' )
    out.code( ' * A constant value which uniquly identifies objects of this type.' )
    out.code( ' */' )
    out.code( 'NIFLIB_API static const Type TYPE;' )
    out.code()
    out.code( '/*!' )
    out.code( ' * A factory function used during file reading to create an instance of this type of object.' )
    out.code( ' * \\return A pointer to a newly allocated instance of this type of object.' )
    out.code( ' */' )
    out.code( 'NIFLIB_API static NiObject * Create();' )
    out.code()
    out.code( '/*!' )
    out.code( ' * Summarizes the information contained in this object in English.' )
    out.code( ' * \\param[in] verbose Determines whether or not detailed information about large areas of data will be printed out.' )
    out.code( ' * \\return A string containing a summary of the information within the object in English.  This is the function that Niflyze calls to generate its analysis, so the output is the same.' )
    out.code( ' */' )
    out.code( 'NIFLIB_API virtual string asString( bool verbose = false ) const;' )
    out.code()
    out.code( '/*!' )
    out.code( ' * Used to determine the type of a particular instance of this object.' )
    out.code( ' * \\return The type constant for the actual type of the object.' )
    out.code( ' */' )
    out.code( 'NIFLIB_API virtual const Type & GetType() const;' )
    out.code()

    #
    # Show example naive implementation if requested
    #
    
    # Create a list of members eligable for functions
    if GENACCESSORS:
        func_members = []
        for y in x.members:
            if not y.arr1_ref and not y.arr2_ref and y.cname.lower().find("unk") == -1:
                func_members.append(y)
    
        if len(func_members) > 0:
            out.code( '/***Begin Example Naive Implementation****' )
            out.code()
            for y in func_members:
                out.comment( y.description + "\n\\return The current value.", False )
                out.code( y.getter_declare("", ";") )
                out.code()
                out.comment( y.description + "\n\\param[in] value The new value.", False )
                out.code(  y.setter_declare("", ";") )
                out.code()
            out.code( '****End Example Naive Implementation***/' )
        else:
            out.code ( '//--This object has no eligable attributes.  No example implementation generated--//' )
        out.code()
    
    out.code( '//--BEGIN MISC CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['MISC']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    if x.members:
        out.code( 'protected:' )
    out.declare(x)
    out.code( 'public:' )
    out.code( '/*! NIFLIB_HIDDEN function.  For internal use only. */' )
    out.code( 'NIFLIB_HIDDEN virtual void Read( istream& in, list<unsigned int> & link_stack, const NifInfo & info );' )
    out.code( '/*! NIFLIB_HIDDEN function.  For internal use only. */' )
    out.code( 'NIFLIB_HIDDEN virtual void Write( ostream& out, const map<NiObjectRef,unsigned int> & link_map, list<NiObject *> & missing_link_stack, const NifInfo & info ) const;' )
    out.code( '/*! NIFLIB_HIDDEN function.  For internal use only. */' )
    out.code( 'NIFLIB_HIDDEN virtual void FixLinks( const map<unsigned int,NiObjectRef> & objects, list<unsigned int> & link_stack, list<NiObjectRef> & missing_link_stack, const NifInfo & info );' )
    out.code( '/*! NIFLIB_HIDDEN function.  For internal use only. */' )
    out.code( 'NIFLIB_HIDDEN virtual list<NiObjectRef> GetRefs() const;' )
    out.code( '/*! NIFLIB_HIDDEN function.  For internal use only. */' )
    out.code( 'NIFLIB_HIDDEN virtual list<NiObject *> GetPtrs() const;' )
    out.code( '};' )
    out.code()
    out.code( '//--BEGIN FILE FOOT CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['FILE FOOT']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.write( "} //End Niflib namespace\n" )
    out.code( '#endif' )
    out.close()

    ##Check if the temp file is identical to the target file
    #OverwriteIfChanged( file_name, 'temp' )

    #
    # NiObject Implementation File
    #

    #Get existing custom code
    file_name = ROOT_DIR + '/src/obj/' + x.cname + '.cpp'
    custom_lines = ExtractCustomCode( file_name );
        
    out = CFile( file_name, 'w')
    out.code( '/* Copyright (c) 2006, NIF File Format Library and Tools' )
    out.code( 'All rights reserved.  Please see niflib.h for license. */' )
    out.code()
    out.code( '//-----------------------------------NOTICE----------------------------------//' )
    out.code( '// Some of this file is automatically filled in by a Python script.  Only    //' )
    out.code( '// add custom code in the designated areas or it will be overwritten during  //' )
    out.code( '// the next update.                                                          //' )
    out.code( '//-----------------------------------NOTICE----------------------------------//' )
    out.code()
    out.code( '//--BEGIN FILE HEAD CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['FILE HEAD']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.code( '#include "../../include/FixLink.h"' )
    out.code( '#include "../../include/ObjectRegistry.h"' )
    out.code( '#include "../../include/NIF_IO.h"' )
    out.code( x.code_include_cpp( True, "../../include/gen/", "../../include/obj/" ) )
    out.code( "using namespace Niflib;" );
    out.code()
    out.code( '//Definition of TYPE constant' )
    if x.inherit:
        out.code ( 'const Type ' + x.cname + '::TYPE(\"' + x.cname + '\", &' + x.inherit.cname + '::TYPE );' )
    else:
        out.code ( 'const Type ' + x.cname + '::TYPE(\"' + x.cname + '\", &RefObject::TYPE );' )
    out.code()
    x_code_construct = x.code_construct()
    if x_code_construct:
        out.code( x.cname + '::' + x.cname + '()' + x_code_construct + ' {' )
    else:
        out.code( x.cname + '::' + x.cname + '() {' )
    out.code ( '//--BEGIN CONSTRUCTOR CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['CONSTRUCTOR']:
        out.write(l);
        
    out.code ( '//--END CUSTOM CODE--//')
    out.code ( '}' )
    
    out.code()
    out.code( x.cname + '::' + '~' + x.cname + '() {' )
    out.code ( '//--BEGIN DESTRUCTOR CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['DESTRUCTOR']:
        out.write(l);
        
    out.code ( '//--END CUSTOM CODE--//')
    out.code ( '}' )
    out.code() 
    out.code( 'const Type & %s::GetType() const {'%x.cname )
    out.code( 'return TYPE;' )
    out.code( '}' )
    out.code()
    out.code( 'NiObject * ' + x.cname + '::Create() {' )
    out.code( 'return new ' + x.cname + ';' )
    out.code( '}' )
    out.code()

    out.code("void %s::Read( istream& in, list<unsigned int> & link_stack, const NifInfo & info ) {"%x.cname)
    out.code( '//--BEGIN PRE-READ CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['PRE-READ']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.stream(x, ACTION_READ)
    out.code()
    out.code( '//--BEGIN POST-READ CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['POST-READ']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code("}")
    out.code()
      
    out.code("void %s::Write( ostream& out, const map<NiObjectRef,unsigned int> & link_map, list<NiObject *> & missing_link_stack, const NifInfo & info ) const {"%x.cname)
    out.code( '//--BEGIN PRE-WRITE CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['PRE-WRITE']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.stream(x, ACTION_WRITE)
    out.code()
    out.code( '//--BEGIN POST-WRITE CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['POST-WRITE']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code("}")
    out.code()
      
    out.code("std::string %s::asString( bool verbose ) const {"%x.cname)
    out.code( '//--BEGIN PRE-STRING CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['PRE-STRING']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.stream(x, ACTION_OUT)
    out.code()
    out.code( '//--BEGIN POST-STRING CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['POST-STRING']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code("}")
    out.code()

    out.code("void %s::FixLinks( const map<unsigned int,NiObjectRef> & objects, list<unsigned int> & link_stack, list<NiObjectRef> & missing_link_stack, const NifInfo & info ) {"%x.cname)

    out.code( '//--BEGIN PRE-FIXLINKS CUSTOM CODE--//' )
    
    #Preserve Custom code from before
    for l in custom_lines['PRE-FIXLINKS']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code()
    out.stream(x, ACTION_FIXLINKS)
    out.code()
    out.code( '//--BEGIN POST-FIXLINKS CUSTOM CODE--//' )
    #Preserve Custom code from before
    for l in custom_lines['POST-FIXLINKS']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )
    out.code("}")
    out.code()

    out.code("std::list<NiObjectRef> %s::GetRefs() const {"%x.cname)
    out.stream(x, ACTION_GETREFS)
    out.code("}")
    out.code()

    out.code("std::list<NiObject *> %s::GetPtrs() const {"%x.cname)
    out.stream(x, ACTION_GETPTRS)
    out.code("}")
    out.code()

    # Output example implementation of public getter/setter Mmthods if requested
    if GENACCESSORS:
        func_members = []
        for y in x.members:
            if not y.arr1_ref and not y.arr2_ref and y.cname.lower().find("unk") == -1:
                func_members.append(y)
    
        if len(func_members) > 0:
            out.code( '/***Begin Example Naive Implementation****' )
            out.code()
            for y in func_members:
                out.code( y.getter_declare(x.name + "::", " {") )
                out.code( "return %s;"%y.cname )
                out.code( "}" )
                out.code()
                
                out.code( y.setter_declare(x.name + "::", " {") )
                out.code( "%s = value;"%y.cname )
                out.code( "}" )
                out.code()
            out.code( '****End Example Naive Implementation***/' )
        else:
            out.code ( '//--This object has no eligable attributes.  No example implementation generated--//' )
        out.code()
        
    out.code( '//--BEGIN MISC CUSTOM CODE--//' )

    #Preserve Custom code from before
    for l in custom_lines['MISC']:
        out.write(l);
        
    out.code( '//--END CUSTOM CODE--//' )

    ##Check if the temp file is identical to the target file
    #OverwriteIfChanged( file_name, 'temp' )

    out.close()
