#include "updater.h"
#include "system.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cstdio>

namespace
{
	QList<int> numbers(const QString& v)
	{
		QList<int> out;
		static const QRegularExpression re("^v?(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?");
		const auto m = re.match(v.trimmed());
		for (int i = 1; i <= 3; i++)
			out << m.captured(i).toInt();
		return out;
	}

	QNetworkRequest githubRequest(const QString& url)
	{
		QNetworkRequest r{QUrl(url)};
		r.setRawHeader("Accept", "application/vnd.github+json");
		r.setHeader(QNetworkRequest::UserAgentHeader, "USCL/" + System::version());
		r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
		return r;
	}
}

Updater::Updater(QObject* parent)
	: QObject(parent)
{
}

void Updater::setState(const QString& state, const QString& message)
{
	m_state = state;
	m_message = message;
	emit changed();
}

int Updater::compareVersions(const QString& a, const QString& b)
{
	const QList<int> x = numbers(a), y = numbers(b);
	for (int i = 0; i < 3; i++)
	{
		if (x[i] != y[i])
			return x[i] - y[i];
	}
	// same numbers: a release beats its pre-release ("0.3.0" > "0.3.0-test")
	const bool pa = a.contains('-'), pb = b.contains('-');
	return pa == pb ? 0 : pa ? -1 : 1;
}

QString Updater::assetName()
{
#ifdef _WIN32
	return "USCL-Setup.exe";
#else
	// only an installed AppImage can replace itself; a build folder or a distro package can't
	return qEnvironmentVariableIsSet("APPIMAGE") ? "USCL-x86_64.AppImage" : QString();
#endif
}

void Updater::check(bool testing)
{
	if (m_state == "checking" || m_state == "downloading")
		return;
	if (assetName().isEmpty())
	{
		setState("unsupported", tr("This copy of USCL can't update itself. Install the AppImage to get updates."));
		return;
	}
	setState("checking");
	QNetworkReply* reply = m_net.get(githubRequest(QString("https://api.github.com/repos/%1/releases?per_page=15").arg(USCL_RELEASES)));
	connect(reply, &QNetworkReply::finished, this, [this, reply, testing] {
		reply->deleteLater();
		const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		if (reply->error() != QNetworkReply::NoError || !doc.isArray())
		{
			setState("error", tr("Couldn't reach the update server."));
			return;
		}
		// newest release this channel takes that has our file
		for (const QJsonValue& rel : doc.array())
		{
			if (rel["draft"].toBool() || (rel["prerelease"].toBool() && !testing))
				continue;
			QString url, sha_url;
			qint64 size = 0;
			for (const QJsonValue& asset : rel["assets"].toArray())
			{
				if (asset["name"].toString() == assetName())
				{
					url = asset["browser_download_url"].toString();
					size = asset["size"].toInteger();
				}
				else if (asset["name"].toString() == assetName() + ".sha256")
				{
					sha_url = asset["browser_download_url"].toString();
				}
			}
			if (url.isEmpty())
				continue;
			const QString tag = rel["tag_name"].toString();
			if (compareVersions(tag, System::version()) <= 0)
				break;
			m_latest = tag.startsWith('v') ? tag.mid(1) : tag;
			m_notes = rel["body"].toString().trimmed();
			m_url = url;
			m_sha_url = sha_url;
			m_size = size;
			setState("available");
			return;
		}
		setState("current", tr("USCL is up to date."));
	});
}

void Updater::download()
{
	if (m_state != "available" && m_state != "error")
		return;
	if (m_url.isEmpty())
		return;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/update";
	QDir().mkpath(dir);
	m_file.setFileName(dir + "/" + assetName() + ".part");
	if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		setState("error", tr("Couldn't save the update."));
		return;
	}
	m_progress = 0;
	m_expected_sha.clear();
	setState("downloading");

	if (!m_sha_url.isEmpty())
	{
		QNetworkReply* sha = m_net.get(githubRequest(m_sha_url));
		connect(sha, &QNetworkReply::finished, this, [this, sha] {
			sha->deleteLater();
			m_expected_sha = QString::fromLatin1(sha->readAll()).section(QRegularExpression("\\s"), 0, 0).trimmed().toLower();
		});
	}
	QNetworkReply* reply = m_net.get(githubRequest(m_url));
	connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_file.write(reply->readAll()); });
	connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
		if (total > 0)
		{
			m_progress = double(got) / double(total);
			emit changed();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		m_file.write(reply->readAll());
		m_file.close();
		if (reply->error() != QNetworkReply::NoError || (m_size > 0 && m_file.size() != m_size))
		{
			m_file.remove();
			setState("error", tr("The download didn't finish. Try again."));
			return;
		}
		if (!m_sha_url.isEmpty())
		{
			QFile f(m_file.fileName());
			QCryptographicHash hash(QCryptographicHash::Sha256);
			if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f) || QString::fromLatin1(hash.result().toHex()) != m_expected_sha)
			{
				m_file.remove();
				setState("error", tr("The update didn't pass its check. Try again."));
				return;
			}
		}
		const QString final_path = m_file.fileName().chopped(5);
		QFile::remove(final_path);
		m_file.rename(final_path);
		setState("ready");
	});
}

void Updater::install()
{
	if (m_state != "ready")
		return;
	const QString file = m_file.fileName();
#ifdef _WIN32
	// the installer closes USCL's files, installs, and starts the new USCL when it's done
	if (!QProcess::startDetached(file, {"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS"}))
	{
		setState("error", tr("Couldn't start the installer."));
		return;
	}
	QCoreApplication::quit();
#else
	// replace the AppImage we run from: copy next to it, then rename over it in one step (fine while it runs)
	const QString target = qEnvironmentVariable("APPIMAGE");
	const QString next = target + ".new";
	QFile::remove(next);
	if (!QFile::copy(file, next) ||
	    !QFile::setPermissions(next, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup |
	                                     QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther) ||
	    std::rename(QFile::encodeName(next).constData(), QFile::encodeName(target).constData()) != 0)
	{
		QFile::remove(next);
		setState("error", tr("Couldn't replace %1.").arg(target));
		return;
	}
	QFile::remove(file);
	QProcess::startDetached(target, {});
	QCoreApplication::quit();
#endif
}
