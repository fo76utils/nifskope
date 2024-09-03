###############################
## BUILD OPTIONS
###############################

*msvc* {
    TEMPLATE = vcapp
} else {
    TEMPLATE = app
}

TARGET   = NifSkope

macx: {
    QMAKE_CC = clang
    QMAKE_CXX = clang++

    ICON = res/nifskope.icns
}

QT += xml opengl network widgets

# Require Qt 5.15 or higher
!contains(QT_VERSION, ^5\\.1[5-9].*) {
	message("Cannot build NifSkope with Qt version $${QT_VERSION}")
	error("Minimum required version is Qt 5.15")
}

# C++ Standard Support (NOTE: c++2a is deprecated by GCC, but is needed for compatibility with Qt 5)
contains(QT_VERSION, ^6.*) {
    CONFIG += c++20
} else:macx {
    CONFIG += c++2a
}

# Dependencies
CONFIG += nvtristrip qhull gli libfo76utils

# Debug/Release options
contains(debug, 1) {
	CONFIG += debug
}
CONFIG(debug, debug|release) {
	# Debug Options
	BUILD = debug
	CONFIG += console
} else {
	# Release Options
	BUILD = release
	CONFIG -= console
	DEFINES += QT_NO_DEBUG_OUTPUT
}

# TODO: Get rid of this define
#	uncomment this if you want the text stats gl option
#	DEFINES += USE_GL_QPAINTER

#TRANSLATIONS += \
#	res/lang/NifSkope_de.ts \
#	res/lang/NifSkope_fr.ts

# Require explicit
DEFINES += \
	_USE_MATH_DEFINES \ # Define M_PI, etc. in cmath
	QT_NO_CAST_FROM_BYTEARRAY \ # QByteArray deprecations
	QT_NO_URL_CAST_FROM_STRING \ # QUrl deprecations
	QT_DISABLE_DEPRECATED_BEFORE=0x050E00 #\ # Disable all functions deprecated as of 5.14

	# Useful for tracking down strings not using
	#	QObject::tr() for translations.
	# QT_NO_CAST_FROM_ASCII \
	# QT_NO_CAST_TO_ASCII


VISUALSTUDIO = false
*msvc* {
	######################################
	## Detect Visual Studio vs Qt Creator
	######################################
	#	Qt Creator = shadow build
	#	Visual Studio = no shadow build

	# Strips PWD (source) from OUT_PWD (build) to test if they are on the same path
	#	- contains() does not work
	#	- equals( PWD, $${OUT_PWD} ) is not sufficient
	REP = $$replace(OUT_PWD, $${PWD}, "")

	# Test if Build dir is outside Source dir
	#	if REP == OUT_PWD, not Visual Studio
	!equals( REP, $${OUT_PWD} ):VISUALSTUDIO = true
	unset(REP)

	# Set OUT_PWD to ./bin so that qmake doesn't clutter PWD
	#	Unfortunately w/ VS qmake still creates empty debug/release folders in PWD.
	#	They are never used but get auto-generated because of CONFIG += debug_and_release
	$$VISUALSTUDIO:OUT_PWD = $${_PRO_FILE_PWD_}/bin
}


###############################
## FUNCTIONS
###############################

include(NifSkope_functions.pri)


###############################
## MACROS
###############################

# NifSkope Version
VER = $$getVersion()
# NifSkope Revision
REVISION = $$getRevision()

# NIFSKOPE_VERSION macro
DEFINES += NIFSKOPE_VERSION=\\\"$${VER}\\\"

# NIFSKOPE_REVISION macro
!isEmpty(REVISION) {
	DEFINES += NIFSKOPE_REVISION=\\\"$${REVISION}\\\"
}


###############################
## OUTPUT DIRECTORIES
###############################

# build_pass is necessary
# Otherwise it will create empty .moc, .ui, etc. dirs on the drive root
build_pass|!debug_and_release {
	win32:equals( VISUALSTUDIO, true ) {
		# Visual Studio
		DESTDIR = $${_PRO_FILE_PWD_}/bin/$${BUILD}
		# INTERMEDIATE FILES
		INTERMEDIATE = $${DESTDIR}/../GeneratedFiles/$${BUILD}
	} else {
		# Qt Creator
		DESTDIR = $${OUT_PWD}/$${BUILD}
		# INTERMEDIATE FILES
		INTERMEDIATE = $${DESTDIR}/../GeneratedFiles/
	}

	UI_DIR = $${INTERMEDIATE}/.ui
	MOC_DIR = $${INTERMEDIATE}/.moc
	RCC_DIR = $${INTERMEDIATE}/.qrc
	OBJECTS_DIR = $${INTERMEDIATE}/.obj
}

###############################
## TARGETS
###############################

include(NifSkope_targets.pri)


###############################
## PROJECT SCOPES
###############################

INCLUDEPATH += src lib

HEADERS += \
	src/data/nifitem.h \
	src/data/niftypes.h \
	src/data/nifvalue.h \
	src/gl/marker/constraints.h \
	src/gl/marker/furniture.h \
	src/gl/BSMesh.h \
	src/gl/bsshape.h \
	src/gl/controllers.h \
	src/gl/glcontroller.h \
	src/gl/glmarker.h \
	src/gl/glmesh.h \
	src/gl/glnode.h \
	src/gl/glparticles.h \
	src/gl/glproperty.h \
	src/gl/glscene.h \
	src/gl/glshape.h \
	src/gl/gltex.h \
	src/gl/gltexloaders.h \
	src/gl/gltools.h \
	src/gl/icontrollable.h \
	src/gl/renderer.h \
	src/io/material.h \
	src/io/MeshFile.h \
	src/io/nifstream.h \
	src/lib/importex/3ds.h \
	src/lib/nvtristripwrapper.h \
	src/lib/qhull.h \
	src/model/basemodel.h \
	src/model/kfmmodel.h \
	src/model/nifmodel.h \
	src/model/nifproxymodel.h \
	src/model/undocommands.h \
	src/spells/blocks.h \
	src/spells/mesh.h \
	src/spells/misc.h \
	src/spells/sanitize.h \
	src/spells/skeleton.h \
	src/spells/stringpalette.h \
	src/spells/tangentspace.h \
	src/spells/texture.h \
	src/spells/transform.h \
	src/ui/widgets/colorwheel.h \
	src/ui/widgets/filebrowser.h \
	src/ui/widgets/fileselect.h \
	src/ui/widgets/floatedit.h \
	src/ui/widgets/floatslider.h \
	src/ui/widgets/groupbox.h \
	src/ui/widgets/inspect.h \
	src/ui/widgets/lightingwidget.h \
	src/ui/widgets/nifcheckboxlist.h \
	src/ui/widgets/nifeditors.h \
	src/ui/widgets/nifview.h \
	src/ui/widgets/refrbrowser.h \
	src/ui/widgets/uvedit.h \
	src/ui/widgets/valueedit.h \
	src/ui/widgets/xmlcheck.h \
	src/ui/about_dialog.h \
	src/ui/checkablemessagebox.h \
	src/ui/settingsdialog.h \
	src/ui/settingspane.h \
	src/xml/nifexpr.h \
	src/xml/xmlconfig.h \
	src/bsamodel.h \
	src/gamemanager.h \
	src/glview.h \
	src/message.h \
	src/nifskope.h \
	src/qtcompat.h \
	src/spellbook.h \
	src/version.h \
	lib/dds.h \
	lib/dxgiformat.h \
	lib/half.h \
	lib/json.hpp \
	lib/meshlet.h \
	lib/meshoptimizer/meshoptimizer.h \
	lib/stb_image.h \
	lib/stb_image_write.h \
	lib/tiny_gltf.h

SOURCES += \
	src/data/nifitem.cpp \
	src/data/niftypes.cpp \
	src/data/nifvalue.cpp \
	src/gl/BSMesh.cpp \
	src/gl/bsshape.cpp \
	src/gl/controllers.cpp \
	src/gl/glcontroller.cpp \
	src/gl/glmarker.cpp \
	src/gl/glmesh.cpp \
	src/gl/glnode.cpp \
	src/gl/glparticles.cpp \
	src/gl/glproperty.cpp \
	src/gl/glscene.cpp \
	src/gl/glshape.cpp \
	src/gl/gltex.cpp \
	src/gl/gltexloaders.cpp \
	src/gl/gltools.cpp \
	src/gl/renderer.cpp \
	src/io/materialfile.cpp \
	src/io/MeshFile.cpp \
	src/io/nifstream.cpp \
	src/lib/importex/3ds.cpp \
	src/lib/importex/importex.cpp \
	src/lib/importex/obj.cpp \
	src/lib/importex/col.cpp \
	src/lib/importex/gltf.cpp \
	src/lib/nvtristripwrapper.cpp \
	src/lib/qhull.cpp \
	src/model/basemodel.cpp \
	src/model/kfmmodel.cpp \
	src/model/nifdelegate.cpp \
	src/model/nifmodel.cpp \
	src/model/nifextfiles.cpp \
	src/model/nifproxymodel.cpp \
	src/model/undocommands.cpp \
	src/spells/animation.cpp \
	src/spells/blocks.cpp \
	src/spells/bounds.cpp \
	src/spells/color.cpp \
	src/spells/fileextract.cpp \
	src/spells/filerename.cpp \
	src/spells/flags.cpp \
	src/spells/fo3only.cpp \
	src/spells/havok.cpp \
	src/spells/headerstring.cpp \
	src/spells/light.cpp \
	src/spells/materialedit.cpp \
	src/spells/mesh.cpp \
	src/spells/meshfilecopy.cpp \
	src/spells/misc.cpp \
	src/spells/moppcode.cpp \
	src/spells/morphctrl.cpp \
	src/spells/normals.cpp \
	src/spells/optimize.cpp \
	src/spells/sanitize.cpp \
	src/spells/sfmatexport.cpp \
	src/spells/simplify.cpp \
	src/spells/skeleton.cpp \
	src/spells/stringpalette.cpp \
	src/spells/strippify.cpp \
	src/spells/tangentspace.cpp \
	src/spells/texture.cpp \
	src/spells/transform.cpp \
	src/ui/widgets/colorwheel.cpp \
	src/ui/widgets/filebrowser.cpp \
	src/ui/widgets/fileselect.cpp \
	src/ui/widgets/floatedit.cpp \
	src/ui/widgets/floatslider.cpp \
	src/ui/widgets/groupbox.cpp \
	src/ui/widgets/inspect.cpp \
	src/ui/widgets/lightingwidget.cpp \
	src/ui/widgets/nifcheckboxlist.cpp \
	src/ui/widgets/nifeditors.cpp \
	src/ui/widgets/nifview.cpp \
	src/ui/widgets/refrbrowser.cpp \
	src/ui/widgets/uvedit.cpp \
	src/ui/widgets/valueedit.cpp \
	src/ui/widgets/xmlcheck.cpp \
	src/ui/about_dialog.cpp \
	src/ui/checkablemessagebox.cpp \
	src/ui/settingsdialog.cpp \
	src/ui/settingspane.cpp \
	src/xml/kfmxml.cpp \
	src/xml/nifexpr.cpp \
	src/xml/nifxml.cpp \
	src/bsamodel.cpp \
	src/gamemanager.cpp \
	src/glview.cpp \
	src/main.cpp \
	src/message.cpp \
	src/nifskope.cpp \
	src/nifskope_ui.cpp \
	src/spellbook.cpp \
	src/version.cpp \
	lib/half.cpp \
	lib/meshlet.cpp \
	lib/meshoptimizer/clusterizer.cpp \
	lib/meshoptimizer/simplifier.cpp \
	lib/meshoptimizer/spatialorder.cpp \
	lib/meshoptimizer/vcacheoptimizer.cpp

RESOURCES += \
	res/nifskope.qrc

FORMS += \
	src/ui/about_dialog.ui \
	src/ui/checkablemessagebox.ui \
	src/ui/nifskope.ui \
	src/ui/settingsdialog.ui \
	src/ui/settingsgeneral.ui \
	src/ui/settingsrender.ui \
	src/ui/settingsresources.ui \
	src/ui/widgets/lightingwidget.ui


###############################
## DEPENDENCY SCOPES
###############################

nvtristrip {
	INCLUDEPATH += lib/NvTriStrip
	HEADERS += \
		lib/NvTriStrip/NvTriStrip.h \
		lib/NvTriStrip/NvTriStripObjects.h \
		lib/NvTriStrip/VertexCache.h
	SOURCES += \
		lib/NvTriStrip/NvTriStrip.cpp \
		lib/NvTriStrip/NvTriStripObjects.cpp \
		lib/NvTriStrip/VertexCache.cpp
}

qhull {
    !*msvc*:QMAKE_CFLAGS += -Ilib/qhull/src
    !*msvc*:QMAKE_CXXFLAGS += -Ilib/qhull/src
    else:INCLUDEPATH += lib/qhull/src
    HEADERS += $$files($$PWD/lib/qhull/src/libqhull/*.h, false)
}

gli {
    !*msvc*:QMAKE_CXXFLAGS += -isystem lib/gli/gli -isystem lib/gli/external
    else:INCLUDEPATH += lib/gli/gli lib/gli/external
    HEADERS += $$files($$PWD/lib/gli/gli/*.hpp, true)
    HEADERS += $$files($$PWD/lib/gli/gli/*.inl, true)
    HEADERS += $$files($$PWD/lib/gli/external/glm/*.hpp, true)
    HEADERS += $$files($$PWD/lib/gli/external/glm/*.inl, true)
}

libfo76utils {
    !*msvc*:QMAKE_CXXFLAGS += -Ilib/libfo76utils/src
    else:INCLUDEPATH += lib/libfo76utils/src
    HEADERS += $$files($$PWD/lib/libfo76utils/src/*.h, false)
    HEADERS += $$files($$PWD/lib/libfo76utils/src/*.hpp, false)
    SOURCES += $$PWD/lib/libfo76utils/src/bits.c
    SOURCES += $$PWD/lib/libfo76utils/src/bptc-tables.c
    SOURCES += $$PWD/lib/libfo76utils/src/decompress-bptc.c
    SOURCES += $$PWD/lib/libfo76utils/src/decompress-bptc-float.c
    SOURCES += $$PWD/lib/libfo76utils/src/ba2file.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/bsmatcdb.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/bsrefl.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/common.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/courb24.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/ddstxt16.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/downsamp.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/esmfile.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/filebuf.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/frtable.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/jsonread.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/matcomps.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/material.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/mat_dump.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/mat_json.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/mat_list.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/pbr_lut.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/sdlvideo.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/sfcube2.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/stringdb.cpp
    # SOURCES += $$PWD/lib/libfo76utils/src/viewrtbl.cpp
    SOURCES += $$PWD/lib/libfo76utils/src/zlib.cpp
}

###############################
## COMPILER SCOPES
###############################

QMAKE_CXXFLAGS_RELEASE -= -O
QMAKE_CXXFLAGS_RELEASE -= -O1
QMAKE_CXXFLAGS_RELEASE -= -O2

win32 {
	RC_FILE = res/icon.rc
	DEFINES += EDIT_ON_ACTIVATE
}

# MSVC
#  Both Visual Studio and Qt Creator
#  Required: msvc2013 or higher
*msvc* {

	# Grab _MSC_VER from the mkspecs that Qt was compiled with
	#	e.g. VS2015 = 1900, VS2017 = 1910
	_MSC_VER = $$find(QMAKE_COMPILER_DEFINES, "_MSC_VER")
	_MSC_VER = $$split(_MSC_VER, =)
	_MSC_VER = $$member(_MSC_VER, 1)

	# Reject unsupported MSVC versions
	!isEmpty(_MSC_VER):lessThan(_MSC_VER, 1900) {
		error("NifSkope only supports MSVC 2015 or later. If this is too prohibitive you may use Qt Creator with MinGW.")
	}

	# So VCProj Filters do not flatten headers/source
	CONFIG -= flat

	# COMPILER FLAGS

	#  Optimization flags
	QMAKE_CXXFLAGS_RELEASE *= -O2
	#  Multithreaded compiling for Visual Studio
	QMAKE_CXXFLAGS += -MP

	# Standards conformance to match GCC and clang
	!isEmpty(_MSC_VER):greaterThan(_MSC_VER, 1900) {
		QMAKE_CXXFLAGS += /permissive- /std:c++20
	}

	# LINKER FLAGS

	#  Relocate .lib and .exp files to keep release dir clean
	QMAKE_LFLAGS += /IMPLIB:$$syspath($${INTERMEDIATE}/NifSkope.lib)

	#  PDB location
	QMAKE_LFLAGS_DEBUG += /PDB:$$syspath($${INTERMEDIATE}/nifskope.pdb)
}


# MinGW, GCC, Clang
#  Recommended: GCC 13+
*-g++|*-clang {

	# COMPILER FLAGS

	# C++ Standard Support
	QMAKE_CXXFLAGS *= -std=c++20

	# Optimization and debugging flags
	QMAKE_CXXFLAGS -= -O0
	QMAKE_CXXFLAGS -= -O1
	QMAKE_CXXFLAGS -= -O2
	QMAKE_CXXFLAGS -= -g
	CONFIG(debug, debug|release) {
		QMAKE_CXXFLAGS *= -Og -ggdb
	} else {
		QMAKE_CXXFLAGS *= -O3
	}
	contains(QMAKE_HOST.arch, x86_64) {
		contains(noavx, 1) {
		} else:contains(nof16c, 1) {
			QMAKE_CXXFLAGS *= -march=sandybridge
		} else:contains(noavx2, 1) {
			QMAKE_CXXFLAGS *= -march=sandybridge -mf16c
		} else {
			QMAKE_CXXFLAGS *= -march=haswell
		}
		QMAKE_CXXFLAGS *= -mtune=generic
	}
}

win32 {
    # GL libs for Qt 5.5+
    LIBS += -lopengl32
}

macx {
	LIBS += -framework CoreFoundation
}


# Pre/Post Link in build_pass only
build_pass|!debug_and_release {

###############################
## QMAKE_PRE_LINK
###############################

	# Find `sed` command
	SED = $$getSed()

	!isEmpty(SED) {
		# Replace @VERSION@ with number from build/VERSION
		# Copy build/README.md.in > README.md
		QMAKE_PRE_LINK += $${SED} -e s/@VERSION@/$${VER}/ $${PWD}/build/README.md.in > $${PWD}/README.md $$nt
	}


###############################
## QMAKE_POST_LINK
###############################

	XML += \
		build/nif.xml \
		build/docsys/kfmxml/kfm.xml

	QSS += \
		res/style.qss

	#LANG += \
	#	res/lang

	SHADERS += \
		res/shaders

	READMES += \
		CHANGELOG.md \
		LICENSE.md \
		README.md \
		README_GLTF.md

	copyDirs( $$SHADERS, shaders )
	#copyDirs( $$LANG, lang )
	copyFiles( $$XML $$QSS res/qt.conf )

	# Copy Readmes and rename to TXT
	copyFiles( $$READMES,,,, md:txt )

	win32:!static {
		# Copy DLLs to build dir
		copyFiles( $$QtBins(),, true )

		platforms += \
			$$[QT_INSTALL_PLUGINS]/platforms/qminimal$${DLLEXT} \
			$$[QT_INSTALL_PLUGINS]/platforms/qwindows$${DLLEXT}
		
		imageformats += \
			$$[QT_INSTALL_PLUGINS]/imageformats/qjpeg$${DLLEXT} \
			$$[QT_INSTALL_PLUGINS]/imageformats/qwebp$${DLLEXT}

		styles += \
			$$[QT_INSTALL_PLUGINS]/styles/qwindowsvistastyle$${DLLEXT}

		copyFiles( $$platforms, platforms, true )
		copyFiles( $$imageformats, imageformats, true )
		copyFiles( $$styles, styles, true )
	}

} # end build_pass


# Build Messages
# (Add `buildMessages` to CONFIG to use)
buildMessages:build_pass|buildMessages:!debug_and_release {
	CONFIG(debug, debug|release) {
		message("Debug Mode")
	} CONFIG(release, release|debug) {
		message("Release Mode")
	}

	message(mkspec _______ $$QMAKESPEC)
	message(cxxflags _____ $$QMAKE_CXXFLAGS)
	message(arch _________ $$QMAKE_TARGET.arch)
	message(src __________ $$PWD)
	message(build ________ $$OUT_PWD)
	message(Qt binaries __ $$[QT_INSTALL_BINS])

	build_pass:equals( VISUALSTUDIO, true ) {
		message(Visual Studio __ Yes)
	}

	#message($$CONFIG)
}

# vim: set filetype=config :
