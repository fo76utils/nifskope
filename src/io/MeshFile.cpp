#include "io/MeshFile.h"
#include "model/nifmodel.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include "fp32vec4.hpp"

#if 0
// x/32767 matches the min/max bounds in BSGeometry more accurately on average
double snormToDouble(int16_t x) { return x < 0 ? x / double(32768) : x / double(32767); }
#endif

MeshFile::MeshFile( const QString & filepath, const NifModel * nif )
{
	QString	path( QDir::fromNativeSeparators(filepath.toLower()) );
	if ( path.isEmpty() || !nif )
		return;

	if ( nif->getResourceFile( data, path, "geometries", ".mesh" ) && readMesh() > 0 ) {
		qDebug() << "MeshFile created for" << filepath;
	} else {
		qWarning() << "MeshFile creation failed for" << filepath;
	}
}

bool MeshFile::isValid()
{
	return !data.isNull();
}

quint32 MeshFile::readMesh()
{
	if ( data.isEmpty() )
		return 0;

	QBuffer f(&data);
	if ( f.open(QIODevice::ReadOnly) ) {
		in.setDevice(&f);
		in.setByteOrder(QDataStream::LittleEndian);
		in.setFloatingPointPrecision(QDataStream::SinglePrecision);

		quint32 magic;
		in >> magic;
		if ( magic != 1 && magic != 2 )
			return 0;

		quint32 indicesSize;
		in >> indicesSize;
		triangles.resize(indicesSize / 3);

		for ( quint32 i = 0; i < indicesSize / 3; i++ ) {
			Triangle tri;
			in >> tri;
			triangles[i] = tri;

		}

		float scale;
		in >> scale;
		if ( scale <= 0.0 )
			return 0; // From RE

		quint32 numWeightsPerVertex;
		in >> numWeightsPerVertex;
		weightsPerVertex = numWeightsPerVertex;

		quint32 numPositions;
		in >> numPositions;
		positions.resize(numPositions + positions.count());

		for ( int i = 0; i < positions.count(); i++ ) {
			uint32_t	xy;
			uint16_t	z;

			in >> xy;
			in >> z;
			FloatVector4	xyz(FloatVector4::convertInt16((std::uint64_t(z) << 32) | xy));
			xyz /= 32767.0f;
			xyz *= scale;

			positions[i] = Vector3(xyz[0], xyz[1], xyz[2]);
		}

		quint32 numCoord1;
		in >> numCoord1;
		coords.resize(numCoord1);

		for ( int i = 0; i < coords.count(); i++ ) {
			uint32_t uv;

			in >> uv;

			coords[i] = Vector4(FloatVector4::convertFloat16(uv));
		}

		quint32 numCoord2;
		in >> numCoord2;
		numCoord2 = std::min(numCoord2, numCoord1);
		haveTexCoord2 = bool(numCoord2);

		for ( quint32 i = 0; i < numCoord2; i++ ) {
			uint32_t uv;

			in >> uv;
			FloatVector4  uv_f(FloatVector4::convertFloat16(uv));

			coords[i][2] = uv_f[0];
			coords[i][3] = uv_f[1];
		}

		quint32 numColor;
		in >> numColor;
		if ( numColor > 0 ) {
			colors.resize(numColor + colors.count());
		}
		for ( quint32 i = 0; i < numColor; i++ ) {
			uint32_t	bgra;
			in >> bgra;
			colors[i] = Color4( ( FloatVector4(bgra) / 255.0f ).shuffleValues( 0xC6 ) );	// 2, 1, 0, 3
		}

		quint32 numNormal;
		in >> numNormal;
		if ( numNormal > 0 ) {
			normals.resize(numNormal + normals.count());
		}
		for ( int i = 0; i < normals.count(); i++ ) {
			quint32	n;
			in >> n;
			FloatVector4	v(FloatVector4::convertX10Y10Z10(n));
			normals[i] = Vector3(v[0], v[1], v[2]);
		}

		quint32 numTangent;
		in >> numTangent;
		if ( numTangent > 0 ) {
			tangents.resize(numTangent + tangents.count());
			tangentsBasis.resize(numTangent + tangentsBasis.count());
			bitangents.resize(numTangent + bitangents.count());
		}
		for ( int i = 0; i < tangents.count(); i++ ) {
			quint32	n;
			in >> n;
			bool	b = bool(n & 0x80000000U);
			FloatVector4	v(FloatVector4::convertX10Y10Z10(n));
			tangents[i] = Vector3(v[0], v[1], v[2]);
			// For export
			tangentsBasis[i] = Vector4(v[0], v[1], v[2], (b) ? 1.0 : -1.0);
			if (b)
				v *= -1.0f;
			v = v.crossProduct3(FloatVector4(normals[i][0], normals[i][1], normals[i][2], 0.0f));
			bitangents[i] = Vector3(v[0], v[1], v[2]);
		}

		quint32 numWeights;
		in >> numWeights;
		if ( numWeights > 0 && numWeightsPerVertex > 0 ) {
			weights.resize(numWeights / numWeightsPerVertex);
		}
		for ( int i = 0; i < weights.count(); i++ ) {
			QVector<QPair<quint16, quint16>> weightsUNORM;
			for ( quint32 j = 0; j < 8; j++ ) {
				if ( j < numWeightsPerVertex ) {
					quint16 b, w;
					in >> b;
					in >> w;
					weightsUNORM.append({ b, w });
				} else {
					weightsUNORM.append({0, 0});
				}
			}
			weights[i] = BoneWeightsUNorm(weightsUNORM, i);
		}

		quint32 numLODs;
		in >> numLODs;
		lods.resize(numLODs);
		for ( quint32 i = 0; i < numLODs; i++ ) {
			quint32 indicesSize2;
			in >> indicesSize2;
			lods[i].resize(indicesSize2 / 3);

			for ( quint32 j = 0; j < indicesSize2 / 3; j++ ) {
				Triangle tri;
				in >> tri;
				lods[i][j] = tri;
			}
		}

		return numPositions;
	}

	return 0;
}
