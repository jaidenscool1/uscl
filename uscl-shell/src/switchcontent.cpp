#include "switchcontent.h"
#include "switchcore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>

namespace
{
	// AES-128 (FIPS-197). The S-box is computed, not typed in; the self-test checks the standard's vector.
	quint8 xtime(quint8 x) { return quint8((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
	quint8 gmul(quint8 a, quint8 b)
	{
		quint8 r = 0;
		for (; b; b >>= 1, a = xtime(a))
			if (b & 1)
				r ^= a;
		return r;
	}

	struct Tables
	{
		std::array<quint8, 256> sbox{}, inv{};
		Tables()
		{
			for (int x = 0; x < 256; x++)
			{
				quint8 b = 0;   // multiplicative inverse (0 for 0)
				for (int y = 1; x && y < 256; y++)
					if (gmul(quint8(x), quint8(y)) == 1)
						b = quint8(y);
				quint8 s = 0x63;
				for (int i = 0; i < 5; i++)
					s ^= quint8((b << i) | (b >> (8 - i)));
				sbox[x] = s;
				inv[s] = quint8(x);
			}
		}
	};
	const Tables& tables()
	{
		static const Tables t;
		return t;
	}

	class Aes128
	{
	public:
		explicit Aes128(const QByteArray& key)
		{
			const auto& sb = tables().sbox;
			std::memcpy(rk, key.constData(), 16);
			quint8 rcon = 1;
			for (int i = 16; i < 176; i += 4)
			{
				quint8 t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
				if (i % 16 == 0)
				{
					const quint8 u = t[0];
					t[0] = sb[t[1]] ^ rcon, t[1] = sb[t[2]], t[2] = sb[t[3]], t[3] = sb[u];
					rcon = xtime(rcon);
				}
				for (int j = 0; j < 4; j++)
					rk[i + j] = rk[i - 16 + j] ^ t[j];
			}
		}
		void encrypt(quint8* s) const
		{
			const auto& sb = tables().sbox;
			add(s, 0);
			for (int r = 1; r <= 10; r++)
			{
				for (int i = 0; i < 16; i++)
					s[i] = sb[s[i]];
				shift(s, false);
				if (r < 10)
					for (int c = 0; c < 4; c++)
					{
						quint8* a = s + 4 * c;
						const quint8 a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
						a[0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
						a[1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
						a[2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
						a[3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
					}
				add(s, r);
			}
		}
		void decrypt(quint8* s) const
		{
			const auto& inv = tables().inv;
			add(s, 10);
			for (int r = 9; r >= 0; r--)
			{
				shift(s, true);
				for (int i = 0; i < 16; i++)
					s[i] = inv[s[i]];
				add(s, r);
				if (r > 0)
					for (int c = 0; c < 4; c++)
					{
						quint8* a = s + 4 * c;
						const quint8 a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
						a[0] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
						a[1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
						a[2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
						a[3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
					}
			}
		}

	private:
		void add(quint8* s, int round) const
		{
			for (int i = 0; i < 16; i++)
				s[i] ^= rk[16 * round + i];
		}
		static void shift(quint8* s, bool inverse)
		{
			quint8 t[16];
			for (int c = 0; c < 4; c++)
				for (int r = 0; r < 4; r++)
					t[4 * c + r] = s[4 * ((inverse ? c - r + 4 : c + r) % 4) + r];
			std::memcpy(s, t, 16);
		}
		quint8 rk[176];
	};

	quint8* bytes(QByteArray& b) { return reinterpret_cast<quint8*>(b.data()); }
	quint64 le64(const QByteArray& b, int at) { return qFromLittleEndian<quint64>(b.constData() + at); }
	quint32 le32(const QByteArray& b, int at) { return qFromLittleEndian<quint32>(b.constData() + at); }

	// the file holding an installed NCA: Eden splits big ones into a <id>.nca folder of 00, 01, ...
	QString ncaFile(const QFileInfo& entry) { return entry.isDir() ? entry.absoluteFilePath() + "/00" : entry.absoluteFilePath(); }
}

QByteArray SwitchContent::aesEncrypt(const QByteArray& key, const QByteArray& block)
{
	QByteArray b = block.left(16);
	if (key.size() != 16 || b.size() != 16)
		return {};
	Aes128(key).encrypt(bytes(b));
	return b;
}

QByteArray SwitchContent::aesDecrypt(const QByteArray& key, const QByteArray& block)
{
	QByteArray b = block.left(16);
	if (key.size() != 16 || b.size() != 16)
		return {};
	Aes128(key).decrypt(bytes(b));
	return b;
}

QByteArray SwitchContent::xts(const QByteArray& key, const QByteArray& data, quint64 sector, bool encrypt)
{
	constexpr int sector_size = 0x200;
	if (key.size() != 32 || data.size() % sector_size)
		return {};
	const Aes128 k1(key.left(16)), k2(key.mid(16));
	QByteArray out = data;
	for (int at = 0; at < out.size(); at += sector_size, sector++)
	{
		quint8 t[16] = {};
		qToBigEndian(sector, t + 8);
		k2.encrypt(t);
		for (int b = at; b < at + sector_size; b += 16)
		{
			quint8* p = bytes(out) + b;
			for (int i = 0; i < 16; i++)
				p[i] ^= t[i];
			encrypt ? k1.encrypt(p) : k1.decrypt(p);
			for (int i = 0; i < 16; i++)
				p[i] ^= t[i];
			quint8 carry = 0;   // t *= x in GF(2^128), little-endian
			for (int i = 0; i < 16; i++)
			{
				const quint8 next = t[i] >> 7;
				t[i] = quint8((t[i] << 1) | carry);
				carry = next;
			}
			if (carry)
				t[0] ^= 0x87;
		}
	}
	return out;
}

QByteArray SwitchContent::ctr(const QByteArray& key, const QByteArray& counter, const QByteArray& data)
{
	if (key.size() != 16 || counter.size() != 16)
		return {};
	const Aes128 k(key);
	QByteArray c = counter, out = data;
	for (int at = 0; at < out.size(); at += 16)
	{
		quint8 stream[16];
		std::memcpy(stream, c.constData(), 16);
		k.encrypt(stream);
		for (int i = 0; i < 16 && at + i < out.size(); i++)
			out[at + i] = char(out[at + i] ^ stream[i]);
		for (int i = 15; i >= 0 && ++bytes(c)[i] == 0; i--) {}
	}
	return out;
}

QMap<QString, QByteArray> SwitchContent::parseKeys(const QByteArray& text)
{
	QMap<QString, QByteArray> keys;
	static const QRegularExpression line(R"((?m)^\s*([a-z0-9_]+)\s*=\s*([0-9a-fA-F]+)\s*$)");
	for (auto it = line.globalMatch(QString::fromLatin1(text)); it.hasNext();)
	{
		const auto m = it.next();
		keys.insert(m.captured(1), QByteArray::fromHex(m.captured(2).toLatin1()));
	}
	return keys;
}

QString SwitchContent::versionString(quint32 v)
{
	return QStringLiteral("v%1").arg(v);
}

int SwitchContent::keyGeneration(const QByteArray& header)
{
	const int generation = std::max(quint8(header[0x206]), quint8(header[0x220]));
	return generation > 0 ? generation - 1 : 0;
}

QByteArray SwitchContent::sectionKey(const QByteArray& header, const QMap<QString, QByteArray>& keys)
{
	// without a title key (rights ID zero) the key area holds the section keys, AES-CTR's the third
	if (header.size() < 0x340 || header.mid(0x230, 16) != QByteArray(16, '\0'))
		return {};
	static const char* const areas[] = {"application", "ocean", "system"};
	const int area = quint8(header[0x207]);
	if (area > 2)
		return {};
	const QByteArray kaek = keys.value(QStringLiteral("key_area_key_%1_%2").arg(areas[area]).arg(keyGeneration(header), 2, 16, QChar('0')));
	return aesDecrypt(kaek, header.mid(0x320, 16));
}

SwitchContent::Firmware SwitchContent::firmware(const QString& dir, const QMap<QString, QByteArray>& keys)
{
	Firmware result;
	const QByteArray header_key = keys.value("header_key");
	if (header_key.size() != 32)
		return result;
	// like the install's copy: the folder's .nca files, or one folder down (a zip's own folder)
	QFileInfoList files = QDir(dir).entryInfoList({"*.nca"}, QDir::Files);
	for (const QFileInfo& sub : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
		if (files.isEmpty())
			files = QDir(sub.absoluteFilePath()).entryInfoList({"*.nca"}, QDir::Files);
	QString system_version;   // the SystemVersion title's data NCA (0100000000000809)
	QByteArray system_header;
	for (const QFileInfo& file : files)
	{
		QFile f(file.absoluteFilePath());
		const QByteArray head = f.open(QIODevice::ReadOnly) ? xts(header_key, f.read(0xC00), 0, false) : QByteArray();
		if (head.size() != 0xC00 || (head.mid(0x200, 4) != "NCA3" && head.mid(0x200, 4) != "NCA2"))
			continue;
		result.readable = true;
		result.generation = std::max(result.generation, keyGeneration(head));
		if (le64(head, 0x210) == 0x0100000000000809ULL && quint8(head[0x205]) == 4)
			system_version = file.absoluteFilePath(), system_header = head;
	}
	result.isFirmware = !system_version.isEmpty();
	if (!result.isFirmware)
		return result;
	// its RomFS (behind an IVFC hash tree, AES-CTR) holds one file: major, minor, micro first
	const QByteArray key = sectionKey(system_header, keys);
	const QByteArray fs = system_header.mid(0x400, 0x200);
	if (key.isEmpty() || fs[2] != 0 || fs[3] != 3 || fs[4] != 3 || fs.mid(0x8, 4) != "IVFC")
		return result;
	QFile f(system_version);
	if (!f.open(QIODevice::ReadOnly))
		return result;
	const qint64 section = qint64(le32(system_header, 0x240)) * 0x200;
	const qint64 romfs = section + qint64(le64(fs, 0x8 + 0x10 + 5 * 0x18));   // level 6 of the tree: the data
	QByteArray upper(8, '\0');
	for (int i = 0; i < 8; i++)
		upper[i] = fs[0x147 - i];
	const auto read = [&](qint64 at, int size) -> QByteArray {
		const qint64 aligned = at & ~qint64(15);
		QByteArray counter = upper + QByteArray(8, '\0');
		qToBigEndian(quint64(aligned >> 4), bytes(counter) + 8);
		if (!f.seek(aligned))
			return {};
		return ctr(key, counter, f.read(at - aligned + size)).mid(int(at - aligned), size);
	};
	const QByteArray header = read(romfs, 0x50);
	if (header.size() != 0x50 || le64(header, 0) != 0x50)
		return result;
	const QByteArray file = read(romfs + qint64(le64(header, 0x48)), 4);   // its one file, at the start of the data
	if (file.size() == 4 && quint8(file[0]) > 0 && quint8(file[0]) < 100)
		result.version = QStringLiteral("%1.%2.%3").arg(quint8(file[0])).arg(quint8(file[1])).arg(quint8(file[2]));
	return result;
}

bool SwitchContent::readMeta(const QString& nca, const QMap<QString, QByteArray>& keys, Meta& meta)
{
	QFile f(nca);
	const QByteArray header_key = keys.value("header_key");
	if (header_key.size() != 32 || !f.open(QIODevice::ReadOnly))
		return false;
	// the first two sectors say what it is; only a content meta needs the rest
	QByteArray head = xts(header_key, f.read(0x400), 0, false);
	if (head.size() != 0x400 || head.mid(0x200, 4) != "NCA3")
		return false;
	meta = {};
	meta.type = quint8(head[0x205]);
	meta.programId = le64(head, 0x210);
	if (meta.type != 1)
		return true;
	const QByteArray rest = xts(header_key, f.read(0x800), 2, false);
	if (rest.size() != 0x800)
		return true;
	head += rest;
	const QByteArray section_key = sectionKey(head, keys);
	const QByteArray fs = head.mid(0x400, 0x200);
	// section 0: a PFS0 (type 1) behind a SHA-256 hash table (2), AES-CTR (3)
	if (section_key.isEmpty() || fs[2] != 1 || fs[3] != 2 || fs[4] != 3)
		return true;
	const qint64 pfs = qint64(le32(head, 0x240)) * 0x200 + qint64(le64(fs, 0x40));
	const qint64 size = qint64(le64(fs, 0x48));
	if (size < 0x10 || size > 1024 * 1024 || pfs < 0xC00)
		return true;
	const qint64 aligned = pfs & ~qint64(15);
	if (!f.seek(aligned))
		return true;
	QByteArray counter(16, '\0');
	for (int i = 0; i < 8; i++)
		counter[i] = fs[0x147 - i];
	qToBigEndian(quint64(aligned >> 4), bytes(counter) + 8);
	const QByteArray data = ctr(section_key, counter, f.read(pfs - aligned + size)).mid(int(pfs - aligned));
	if (data.size() != size || !data.startsWith("PFS0"))
		return true;
	const quint32 count = le32(data, 4), strings = le32(data, 8);
	const qint64 table = 0x10 + qint64(count) * 0x18;
	if (count == 0 || count > 64 || table + strings > size)
		return true;
	for (quint32 i = 0; i < count; i++)
	{
		const int entry = 0x10 + int(i) * 0x18;
		const qint64 at = table + strings + qint64(le64(data, entry)), length = qint64(le64(data, entry + 8));
		const QByteArray name = data.mid(int(table + le32(data, entry + 0x10)), int(strings)).split('\0').value(0);
		if (name.endsWith(".cnmt") && length >= 0x20 && at + length <= size)
		{
			meta.version = le32(data, int(at) + 8);
			meta.metaType = quint8(data[int(at) + 0xC]);
			break;
		}
	}
	return true;
}

SwitchContent::Installed SwitchContent::installed(const QString& path, const QString& serial)
{
	Installed result;
	// an extracted dump runs from base/ or the update folder it's in (updates/<title version>/exefs/main)
	const QStringList parts_of_path = QDir::fromNativeSeparators(path).split('/');
	if (parts_of_path.size() >= 4 && parts_of_path.last() == "main" && parts_of_path.value(parts_of_path.size() - 2) == "exefs")
	{
		const QString name = parts_of_path.value(parts_of_path.size() - 3);
		bool number = false;
		const quint32 v = name.toUInt(&number);
		if (parts_of_path.value(parts_of_path.size() - 4) == "updates" && number)
			result.update = versionString(v), result.build = "dump-" + name;
		else
			result.build = "base";
		return result;
	}
	bool ok = false;
	const quint64 base = serial.toULongLong(&ok, 16);
	if (serial.size() != 16 || !ok)
		return result;
	QFile key_file(SwitchCore::dataDir() + "keys/prod.keys");
	if (!key_file.open(QIODevice::ReadOnly) || key_file.size() > 1024 * 1024)
		return result;
	const auto keys = parseKeys(key_file.readAll());
	if (keys.value("header_key").size() != 32)
		return result;
	QStringList parts;
	QSet<quint64> dlc;
	qint64 newest = -1;
	bool update = false;
	QDirIterator it(SwitchCore::dataDir() + "nand/user/Contents/registered", {"*.nca"}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
	                QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		const QFileInfo entry(it.next());
		Meta meta;
		// the game's update is base + 0x800, its DLC base + 0x1001 and up
		if (!readMeta(ncaFile(entry), keys, meta) || meta.type != 1 || (meta.programId & ~quint64(0x1fff)) != base || meta.programId == base)
			continue;
		const QString content_id = entry.fileName().section('.', 0, 0).toLower();
		parts << QStringLiteral("%1 %2 %3").arg(meta.programId, 16, 16, QChar('0')).arg(content_id).arg(meta.version);
		if ((meta.programId & 0x1fff) == 0x800)
			update = true, newest = std::max(newest, meta.version);
		else
			dlc.insert(meta.programId);
	}
	if (update)
		result.update = newest >= 0 ? versionString(quint32(newest)) : QStringLiteral("?");
	result.dlc = int(dlc.size());
	parts.sort();
	result.build = parts.isEmpty() ? QStringLiteral("base")
	                               : QString::fromLatin1(QCryptographicHash::hash(parts.join('\n').toLatin1(), QCryptographicHash::Sha256).toHex().left(16));
	return result;
}

QString SwitchContent::label(const Installed& c)
{
	QString text = c.update.isEmpty() ? QCoreApplication::translate("SwitchContent", "No update")
	             : c.update == "?"    ? QCoreApplication::translate("SwitchContent", "Update installed")
	                                  : QCoreApplication::translate("SwitchContent", "Update %1").arg(c.update);
	if (c.dlc > 0)
		text += QCoreApplication::translate("SwitchContent", ", %n DLC", "", c.dlc);
	return text;
}
