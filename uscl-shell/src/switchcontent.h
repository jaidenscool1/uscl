#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

// What a Switch game has beyond its base: the update and DLC Eden has installed for it (nand/user/Contents/
// registered), or for an extracted dump, the update folder it runs from. Players in a session must match, so the
// host's build goes in the session's game field and members compare theirs before starting (like the PS3's EBOOT
// hash). Read from the content metas: each NCA header is decrypted with the player's header_key, the update's
// version from its CNMT with the key area key. Nothing is downloaded.
class SwitchContent
{
public:
	struct Installed
	{
		QString update;   // "v524288" (the title version, as NSP names tag it); "?" version unreadable; empty: none
		int dlc = 0;
		QString build;    // same build <=> same update and DLC; "base" for neither; empty when it can't be read
	};
	// path: the game's file or extracted exefs/main; serial: its 16-digit title ID
	static Installed installed(const QString& path, const QString& serial);
	// "Update v524288, 2 DLC" / "No update"
	static QString label(const Installed& content);

	// building blocks, public for the self-test
	struct Meta
	{
		quint64 programId = 0;
		int type = -1;          // NCA content type: 1 = meta
		qint64 version = -1;    // the CNMT's title version, -1 if unread
		int metaType = 0;       // 0x80 application, 0x81 patch (update), 0x82 add-on content (DLC)
	};
	// a decrypted NCA3's content type and program ID, and for a content meta its version; false if not an NCA
	static bool readMeta(const QString& nca, const QMap<QString, QByteArray>& keys, Meta& meta);
	// prod.keys: name -> key bytes
	static QMap<QString, QByteArray> parseKeys(const QByteArray& text);
	// title version -> "v524288". A game's title version counts its updates (x 65536); the "3.0.1" players see is in
	// the update's control data, behind its title key, so it isn't shown.
	static QString versionString(quint32 version);

	// A folder of system software (.nca files, or one folder down) read with these keys: whether it is a firmware set
	// (it has the SystemVersion title), its version ("22.5.0", from that title, empty if unread) and the newest key
	// generation its NCAs need. readable: the header_key opened at least one of them.
	struct Firmware
	{
		bool readable = false, isFirmware = false;
		QString version;
		int generation = -1;
	};
	static Firmware firmware(const QString& dir, const QMap<QString, QByteArray>& keys);
	// an NCA header's key generation (master key index), and its AES-CTR section key from the key area
	static int keyGeneration(const QByteArray& header);
	static QByteArray sectionKey(const QByteArray& header, const QMap<QString, QByteArray>& keys);

	static QByteArray aesEncrypt(const QByteArray& key, const QByteArray& block);
	static QByteArray aesDecrypt(const QByteArray& key, const QByteArray& block);
	// Nintendo's AES-XTS: 0x200-byte sectors, sector number as a big-endian tweak; key = data key + tweak key
	static QByteArray xts(const QByteArray& key, const QByteArray& data, quint64 sector, bool encrypt);
	static QByteArray ctr(const QByteArray& key, const QByteArray& counter, const QByteArray& data);
};
