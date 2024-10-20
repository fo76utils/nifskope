/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#ifndef VERSION_H
#define VERSION_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>


/*!
 * @file	version.h NifSkopeVersion
 * @author	jonwd7
 * @date	2014-06-06
 * 
 * @see Github: https://github.com/niftools/nifskope/issues/61
 */ 


// Forward declarations
class QDebug;


//! Define current NifSkope version as hex int
#define NIFSKOPE_VERSION_HEX NifSkopeVersion::hexVersion( NIFSKOPE_VERSION )


/*! Encapsulates application version strings into comparable objects and provides static convenience
 * functions for raw strings.
 *
 * For comparison purposes, such as for migrating QSettings between versions,
 * or removing deprecated QSettings.
 *
 * For formatting purposes, such as display strings for window titles.
 */
class NifSkopeVersion final
{

public:
	NifSkopeVersion( const QString & ver );
	NifSkopeVersion( const NifSkopeVersion & other );
	~NifSkopeVersion();

	//! Raw string as stored in VERSION file or registry
	const QString rawVersion;
	//! Display string formatted for readability
	const QString displayVersion;

	//! Instance version of NifSkopeVersion::versionParts()
	QList<int> parts() const;
	//! Instance version of NifSkopeVersion::rawToMajMin()
	QString majMin() const;
	//! Instance version of NifSkopeVersion::hexVersion()
	int hex() const;

	/*! Compare two strings beyond MAJ.MIN.REV granularity
	 * 
	 * Max of 7 parts:
	 *	0 = Major
	 *	1 = Minor
	 *	2 = Revision
	 *	3 = Stage Code (Alpha, Beta, RC, Final)
	 *	4 = Stage Version
	 *	5 = Dev Code (dev, post)
	 *	6 = Dev Version
	 * 
	 * @param	parts	Number of parts
	 * @return	void
	 *
	 * @note This sets NifSkopeVersion::numParts (static) for *all* NifSkopeVersion objects.
	 * @code
	 *		// Default numParts = 3
	 *		NifSkopeVersion a( "1.2.0" );
	 *		NifSkopeVersion b( "1.2.0a1" );
	 *		
	 *		qDebug() << (a > b); // False
	 *		
	 *		NifSkopeVersion::setNumParts( 5 );
	 *		
	 *		qDebug() << (a > b); // True
	 * @endcode
	 */
	static void setNumParts( int num );

	/*! Integer representation of version string
	 *	represented in hex e.g. `"1.2.1" -> 0x010201 -> 66049`
	 */
	static int hexVersion( const QString );
	static int hexVersion( const QList<int> );

	/*! Compare two strings beyond MAJ.MIN.REV granularity
	 *
	 * @param[in]	ver1				Version 1
	 * @param[in]	ver2				Version 2
	 * @param		parts				Number of parts to compare
	 * @return		`{-1, 0, 1}` meaning: `{ver1 < ver2, ver1 == ver2, ver1 > ver2}`
	 */
	static int compare( const QString & ver1, const QString & ver2, int parts );
	static int compare( const QString & ver1, const QString & ver2 );

	/*! Compare two strings beyond MAJ.MIN.REV granularity
	 *
	 * @param[in]	ver1				Version 1
	 * @param[in]	ver2				Version 2
	 * @param		parts				Number of parts to compare
	 * @return		True if `ver1 > ver2`
	 */
	static bool compareGreater( const QString & ver1, const QString & ver2, int parts );
	static bool compareGreater( const QString & ver1, const QString & ver2 );

	/*! Compare two strings beyond MAJ.MIN.REV granularity
	 *
	 * @param[in]	ver1				Version 1
	 * @param[in]	ver2				Version 2
	 * @param		parts				Number of parts to compare
	 * @return		True if `ver1 < ver2`
	 */
	static bool compareLess( const QString & ver1, const QString & ver2, int parts );
	static bool compareLess( const QString & ver1, const QString & ver2 );

	/*! Version string for display
	 *
	 * @param[in]	ver					Version string
	 * @param		showStage (false)	Whether to show release stage e.g. "1.2.0 Alpha 1"
	 * @param		showDev (false)		Whether to show dev release e.g. "1.2.0 Alpha 1 Dev 1"
	 * @return		Display string for `ver`, optionally formatted with stage/dev information
	 */
	static QString rawToDisplay( const QString & ver, bool showStage = false, bool showDev = false );

	/*! Version string for MAJ.MIN format
	 *
	 * @param[in]	ver			Version string
	 * @return		`ver` formatted to MAJ.MIN format, e.g. 1.2
	 */
	static QString rawToMajMin( const QString & ver );

	/*! Version parts for any version string
	 *
	 * @param[in]	ver			Version string
	 * @param		parts (7)	Number of parts to return
	 * @return		Version parts of length `parts`
	 */
	static QList<int> versionParts( const QString & ver, int parts = 7 );

	/*! Pass by reference a QStringList for the version parts.
	 *
	 *	Some examples of version strings:
	 *
	 *	Valid
	 *		"1.0.0",
	 *		"1.0.1a1",
	 *		"1.0.2b1.dev1",
	 *		"1.1.dev1",
	 *		"1.1.12a1",
	 *		"1.1.12.post1"
	 *
	 *	Invalid, but compensated for
	 *		"1.1.3.a1.dev2",
	 *		"1.2.0a.dev1",
	 *		"1.3.0a",
	 *		"1.4.0rc"
	 *
	 * @param[in]	ver			Version string
	 * @param[out]	verNums		Version string encoded as a list of integers
	 * @param		parts (3)	Number of parts to return
	 * @return		True if valid string, false if invalid string
	 */
	static bool formatVersion( const QString & ver, QList<int> & verNums, int parts = 3 );

	// Compare two NifSkopeVersions
	bool operator<(const NifSkopeVersion & other) const;
	bool operator<=(const NifSkopeVersion & other) const;
	bool operator==(const NifSkopeVersion & other) const;
	bool operator!=(const NifSkopeVersion & other) const;
	bool operator>(const NifSkopeVersion & other) const;
	bool operator>=(const NifSkopeVersion & other) const;

	// Compare a NifSkopeVersion to a QString
	bool operator<(const QString & other) const;
	bool operator<=(const QString & other) const;
	bool operator==(const QString & other) const;
	bool operator!=(const QString & other) const;
	bool operator>(const QString & other) const;
	bool operator>=(const QString & other) const;

protected:
	NifSkopeVersion();

	//! Number of version parts (Default = 3) \note Global across all instances.
	static int numParts;
};

/*! QDebug operator for NifSkopeVersion
 *
 *	Prints rawVersion, displayVersion, parts()
 *		e.g. "1.2.0a1.dev19" "1.2.0 Alpha 1" (1, 2, 0, 1, 1, 0, 19)
 */
QDebug operator<<(QDebug dbg, const NifSkopeVersion & ver);


#endif
