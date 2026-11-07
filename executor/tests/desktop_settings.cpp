/* The desktop application's settings.json is executor/server's
   (frontends/desktop/settings.h): a file the server wrote
   (fixtures/settings/server.json, settings.go's saveSettings) loads, its
   server-scoped settings kept when the desktop saves; what the desktop
   writes has version 2 and only values the schema allows
   (fixtures/settings/desktop.json, which executor/server's
   settings_desktop_test.go loads; EXECUTOR_DESKTOP_SETTINGS_OUT=DIR writes
   it again). */
#include "gtest/gtest.h"

#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace
{
QJsonObject read_json(const QString &path)
{
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::ReadOnly)) << path.toStdString();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString fixture(const char *name)
{
    return QString(EXECUTOR_SOURCE_DIR) + "/tests/fixtures/settings/" + name;
}
}

TEST(DesktopSettings, AServersFileLoadsAndKeepsItsServerSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(QFile::copy(fixture("server.json"), dir.path() + "/settings.json"));
    Settings s(dir.path());
    EXPECT_EQ("1024x768", s.str("machine.screen"));
    EXPECT_EQ("ctrl", s.str("input.command_key"));
    EXPECT_EQ(40, s.integer("sound.volume"));
    EXPECT_EQ(QStringList{ "/library/games.img" }, s.list("disks.mounted"));
    EXPECT_TRUE(s.set("display.smoothing", true));

    QJsonObject saved = read_json(dir.path() + "/settings.json");
    EXPECT_EQ(2, saved["version"].toInt());
    EXPECT_EQ(9000, saved["network"].toObject()["port"].toInt());   /* the server's own */
    EXPECT_TRUE(saved["display"].toObject()["smoothing"].toBool());
    EXPECT_EQ("1024x768", saved["machine"].toObject()["screen"].toString());
}

TEST(DesktopSettings, WritesVersionTwoAndOnlyAllowedValues)
{
    QTemporaryDir dir;
    Settings s(dir.path());
    EXPECT_TRUE(s.set("machine.screen", "800x600"));
    EXPECT_TRUE(s.set("machine.system", "/library/boot-8.6.img"));
    EXPECT_TRUE(s.set("machine.ppc", true));
    EXPECT_TRUE(s.set("sound.volume", 55));
    EXPECT_TRUE(s.set("input.right_click", "click"));
    EXPECT_TRUE(s.set("disks.mounted", QJsonArray{ "/library/utilities.img", "/library/games.iso" }));
    /* refused: the server would refuse the file */
    EXPECT_FALSE(s.set("machine.screen", "huge"));
    EXPECT_FALSE(s.set("machine.system", QString(300, 'x')));
    EXPECT_FALSE(s.set("sound.volume", 101));
    EXPECT_FALSE(s.set("machine.depth", 3));
    EXPECT_FALSE(s.set("input.command_key", "shift"));
    EXPECT_FALSE(s.set("machine.ppc", "yes"));
    EXPECT_FALSE(s.set("disks.mounted", QJsonArray{ "" }));
    EXPECT_EQ("800x600", s.str("machine.screen"));
    EXPECT_EQ(55, s.integer("sound.volume"));

    QJsonObject saved = read_json(dir.path() + "/settings.json");
    EXPECT_EQ(2, saved["version"].toInt());
    EXPECT_EQ("800x600", saved["machine"].toObject()["screen"].toString());

    /* the same as the committed fixture executor/server loads */
    if(qEnvironmentVariableIsSet("EXECUTOR_DESKTOP_SETTINGS_OUT"))
    {
        QString out = qEnvironmentVariable("EXECUTOR_DESKTOP_SETTINGS_OUT") + "/desktop.json";
        QFile::remove(out);
        ASSERT_TRUE(QFile::copy(dir.path() + "/settings.json", out));
    }
    else
        EXPECT_EQ(read_json(fixture("desktop.json")), saved);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);  /* the schema is a Qt resource */
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
