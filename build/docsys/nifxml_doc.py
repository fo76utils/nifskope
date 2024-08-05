# nifxml_doc.py
#
# This script generates HTML documentation from the XML file.
#
# --------------------------------------------------------------------------
# Command line options
#
# -p /path/to/doc : specifies the path where HTML documentation must be created
#
# --------------------------------------------------------------------------
# ***** BEGIN LICENSE BLOCK *****
#
# Copyright (c) 2005, 2006, 2007 NIF File Format Library and Tools
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
# ***** END LICENCE BLOCK *****
# --------------------------------------------------------------------------

from nifxml import *
from distutils.dir_util import mkpath
import os
import itertools

#
# global data
#

ROOT_DIR = "."


prev = ""
for i in sys.argv:
    if prev == "-p":
        ROOT_DIR = i
    prev = i

#
# Sort Name Lists
#

block_names.sort()
compound_names.sort()
basic_names.sort()
enum_names.sort()
flag_names.sort()

def tohex(value, nbytes=4):
    """Improved version of hex."""
    return ("0x%%0%dX" % (2*nbytes)) % (long(str(value)) & (2**(nbytes*8)-1))

def ListAttributes( compound ):
    attr_list = ""
    count  = 0

    #Create Attribute List
    for a in compound.members:
        temp.set_var( "attr-name", a.name )
        attr_type = '<a href="%s.html"><b>%s<b></a>'%(a.type, a.type)
        if a.template:
            attr_type += '&lt;<a href="%s.html">%s</a>&gt;'%(a.template.replace("\\", "_"), a.template)
        
        temp.set_var( "attr-type", attr_type )
        temp.set_var( "attr-arg", a.arg )
        temp.set_var( "attr-arr1", a.arr1.lhs )
        temp.set_var( "attr-arr2", a.arr2.lhs )
        cond_string = a.cond.code("", brackets = False)
        if cond_string:
            temp.set_var( "attr-cond", cond_string )
        else:
            temp.set_var( "attr-cond", "" )

        if count % 2 == 0:
            temp.set_var( "row-class", "reg0" )
        else:
            temp.set_var( "row-class", "reg1" )

        temp.set_var( "attr-desc", a.description.replace("\n", "<br/>") )

        temp.set_var( "attr-from", a.orig_ver1 )
        temp.set_var( "attr-to", a.orig_ver2 )

        attr_list += temp.parse( "templates/attr_row.html" )

        count += 1

    return attr_list

#
# Generate Version List Page
#

temp = Template()
temp.set_var( "title", "NIF File Format Versions" )

#List each Version with Description

count = 0
version_list = ""
for n in version_names:
    x = version_types[n]

    if count % 2 == 0:
        temp.set_var( "row-class", "reg0" )
    else:
        temp.set_var( "row-class", "reg1" )
            
    temp.set_var( "list-name", x.num )
    temp.set_var( "list-desc", x.description.replace("\n", "<br/>") )
    
    version_list += temp.parse( "templates/version_row.html" )

    count += 1


temp.set_var( "list", version_list )

temp.set_var( "contents", temp.parse( "templates/version_list.html") )

f = file(ROOT_DIR + '/doc/version_list.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
        

#
# Generate Basic List Page
#

temp = Template()
temp.set_var( "title", "Basic Data Types" )

#List each Basic Type with Description

count = 0
basic_list = ""
for n in basic_names:
    x = basic_types[n]

    if count % 2 == 0:
        temp.set_var( "row-class", "reg0" )
    else:
        temp.set_var( "row-class", "reg1" )
            
    temp.set_var( "list-name", x.name )
    temp.set_var( "list-desc", x.description.replace("\n", "<br/>") )

    basic_list += temp.parse( "templates/list_row.html" )

    count += 1


temp.set_var( "list", basic_list )

temp.set_var( "contents", temp.parse( "templates/list.html") )

f = file(ROOT_DIR + '/doc/basic_list.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
    

    
#
# Generate Basic Pages
#

count = 0
for n in basic_names:
    x = basic_types[n]

    temp = Template()
    temp.set_var( "title", x.name )
    temp.set_var( "name", x.name )
    temp.set_var( "description", x.description.replace("\n", "<br/>") )
    if count == "1":
        temp.set_var( "count", "<p>Yes</p>" )
    else:
        temp.set_var( "count", "<p>No</p>" )

    #Create Found In list
    found_in = ""

    for b in block_names:
        for m in block_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    for b in compound_names:
        for m in compound_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    temp.set_var( "found-in", found_in );
    
    temp.set_var( "contents", temp.parse( "templates/basic.html") )

    f = file(ROOT_DIR + '/doc/' + x.cname.replace('\\', '_') + '.html', 'w')
    f.write( temp.parse( "templates/main.html" ) )
    f.close()

#
# Generate Enum List Page
#

temp = Template()
temp.set_var( "title", "Enum Data Types" )

#List each Enum Type with Description

count = 0
enum_list = ""
for n, x in itertools.chain(enum_types.iteritems(), flag_types.iteritems()):
    if count % 2 == 0:
        temp.set_var( "row-class", "reg0" )
    else:
        temp.set_var( "row-class", "reg1" )
            
    temp.set_var( "list-name", x.name )
    temp.set_var( "list-desc", x.description.replace("\n", "<br/>") )

    enum_list += temp.parse( "templates/list_row.html" )

    count += 1


temp.set_var( "list", enum_list )

temp.set_var( "contents", temp.parse( "templates/list.html") )

f = file(ROOT_DIR + '/doc/enum_list.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
    

    
#
# Generate Enum Pages
#

count = 0
for n, x in itertools.chain(enum_types.iteritems(), flag_types.iteritems()):

    temp = Template()
    temp.set_var( "title", x.name )
    temp.set_var( "name", x.name )
    temp.set_var( "storage", x.storage )
    temp.set_var( "description", x.description.replace("\n", "<br/>") )

    #Create Found In list
    found_in = ""

    for b in block_names:
        for m in block_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    for b in compound_names:
        for m in compound_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    temp.set_var( "found-in", found_in );

    #Create Choice List

    count = 0
    choice_list = ""
    for o in x.options:
        if count % 2 == 0:
            temp.set_var( "row-class", "reg0" )
        else:
            temp.set_var( "row-class", "reg1" )

        # represent bit flags with hex
        if (hasattr(o, "bit")):
            temp.set_var( "enum-number", tohex(o.value) )
        else:
            temp.set_var( "enum-number", o.value )
        temp.set_var( "enum-name", o.name )
        temp.set_var( "enum-desc", o.description.replace("\n", "<br/>") )

        choice_list += temp.parse( "templates/enum_row.html" )

        count += 1

    temp.set_var( "choices", choice_list )
    
    temp.set_var( "contents", temp.parse( "templates/enum.html") )

    f = file(ROOT_DIR + '/doc/' + x.cname.replace("\\", "_") + '.html', 'w')
    f.write( temp.parse( "templates/main.html" ) )
    f.close()


#
# Generate Compound List Page
#

temp = Template()
temp.set_var( "title", "Compound Data Types" )

#List each Compound with Description

count = 0
compound_list = ""
for n in compound_names:
    x = compound_types[n]

    if count % 2 == 0:
        temp.set_var( "row-class", "reg0" )
    else:
        temp.set_var( "row-class", "reg1" )
            
    temp.set_var( "list-name", x.name )
    temp.set_var( "list-desc", x.description.replace("\n", "<br/>") )

    compound_list += temp.parse( "templates/list_row.html" )

    count += 1


temp.set_var( "list", compound_list )

temp.set_var( "contents", temp.parse( "templates/list.html") )

f = file(ROOT_DIR + '/doc/compound_list.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
    

    
#
# Generate Compound Pages
#

count = 0
for n in compound_names:
    x = compound_types[n]

    temp = Template()
    temp.set_var( "title", x.name )
    temp.set_var( "name", x.name )
    temp.set_var( "description", x.description.replace("\n", "<br/>") )

    #Create Found In list
    found_in = ""

    for b in block_names:
        for m in block_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    for b in compound_names:
        for m in compound_types[b].members:
            if m.type == n:
                found_in += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"
                break

    temp.set_var( "found-in", found_in );

    #Create Attribute List
    attr_list = ListAttributes( x)

    temp.set_var( "attributes", attr_list )
    
    temp.set_var( "contents", temp.parse( "templates/compound.html") )

    f = file(ROOT_DIR + '/doc/' + x.cname.replace("\\", "_") + '.html', 'w')
    f.write( temp.parse( "templates/main.html" ) )
    f.close()

#
# Generate NiObject List Page
#

temp = Template()
temp.set_var( "title", "NIF Object List" )

#List each NiObject with Description

count = 0
niobject_list = ""
for n in block_names:
    x = block_types[n]

    if count % 2 == 0:
        temp.set_var( "row-class", "reg0" )
    else:
        temp.set_var( "row-class", "reg1" )
            
    temp.set_var( "list-name", x.name )
    temp.set_var( "list-desc", x.description.replace("\n", "<br/>") )

    niobject_list += temp.parse( "templates/list_row.html" )

    count += 1


temp.set_var( "list", niobject_list )

temp.set_var( "niobject-contents", temp.parse( "templates/list.html") )
temp.set_var( "contents", temp.parse( "templates/niobject_nav.html") )

f = file(ROOT_DIR + '/doc/niobject_list.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
    

    
#
# Generate NiObject Pages
#

count = 0
for n in block_names:
    x = block_types[n]

    temp = Template()
    temp.set_var( "title", x.name )
    temp.set_var( "name", x.name )
    temp.set_var( "description", x.description.replace("\n", "<br/>") )

    #Create Ancestor List

    ancestors = []
    b = x
    while b:
        ancestors.append(b)
        b = b.inherit

    ancestors.reverse()
        
    #Create Attribute List
    attr_list = ""
    count = 0

    for a in ancestors:
        temp.set_var( "inherit", a.name )
        attr_list += temp.parse( "templates/inherit_row.html" )

        inherit_list = ""
        inherit_list = ListAttributes( a )

        attr_list += inherit_list
    
    temp.set_var( "attributes", attr_list )

    #Create Parent Of list
    parent_of = ""
    for b in block_names:
        if block_types[b].inherit == x:
            parent_of += "<li><a href=\"" + b.replace("\\", "_") + ".html\">" + b + "</a></li>\n"

    temp.set_var( "parent-of", parent_of );
    
    temp.set_var( "contents", temp.parse( "templates/niobject.html") )

    f = file(ROOT_DIR + '/doc/' + x.cname.replace("\\", "_") + '.html', 'w')
    f.write( temp.parse( "templates/main.html" ) )
    f.close()

#global value
object_tree = ""

def ListObjectTree( root ):

    global object_tree

    #get first line of description
    lines = root.description.splitlines(False)
    if len(lines) > 0:
        desc = lines[0]
    else:
        desc = ""

    #add a new list for this ancestor
    object_tree +=  "<li><a href=\"" + root.name + ".html\"><b>" + root.name + "</b></a> | " + desc + "</li>\n"
    """
    <ul>
    <li>
    <a href="index.php?mode=list&amp;table=attr&amp;block_id=379&amp;version="><b>NiObject</b></a>
     | Abstract block type.<ul>
    <li>
    """

    #Create Child List

    children = []
    for b in block_names:
        if block_types[b].inherit == root:
            children.append(block_types[b])

    if len(children) > 0:
        object_tree += "<ul>\n"
        
        for c in children:
            ListObjectTree(c)

        object_tree += "</ul>\n"

#
# Generate NiObject Hierarchy Page
#

temp = Template()
temp.set_var( "title", "NIF Object Hierarchy" )

# Build Tree

object_tree = ""
ListObjectTree( block_types["NiObject"] )
temp.set_var( "object-tree", object_tree )


temp.set_var( "niobject-contents", temp.parse( "templates/hierarchy.html") )
temp.set_var( "contents", temp.parse( "templates/niobject_nav.html") )

f = file(ROOT_DIR + '/doc/index.html', 'w')
f.write( temp.parse( "templates/main.html" ) )
f.close()
        
