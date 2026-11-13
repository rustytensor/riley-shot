// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <QXmlStreamWriter>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlschemas.h>

namespace rileyshot {

namespace {

const char kNamespace[] = "urn:riley-shot:config";

// Collects libxml2's messages instead of letting them reach stderr.
void collect(void *data, const xmlError *e)
{
    if (!data || !e)
        return;
    auto *messages = static_cast<QStringList *>(data);
    QString text = QString::fromUtf8(e->message ? e->message : "unknown error").trimmed();
    if (e->line > 0)
        text = QStringLiteral("line %1: %2").arg(e->line).arg(text);
    messages->append(text);
}

QString text(xmlNode *node)
{
    xmlChar *content = xmlNodeGetContent(node);
    const QString s = QString::fromUtf8(reinterpret_cast<const char *>(content));
    xmlFree(content);
    return s;
}

// xs:boolean after validation: "true"/"1" or "false"/"0", with surrounding whitespace collapsed.
bool boolean(xmlNode *node)
{
    const QString v = text(node).trimmed();
    return v == QLatin1String("true") || v == QLatin1String("1");
}

bool named(xmlNode *node, const char *name)
{
    return node->type == XML_ELEMENT_NODE && node->ns && node->ns->href &&
           xmlStrEqual(node->ns->href, reinterpret_cast<const xmlChar *>(kNamespace)) &&
           xmlStrEqual(node->name, reinterpret_cast<const xmlChar *>(name));
}

// Reads a document that has validated against the schema.
void apply(xmlNode *root, Config *c)
{
    for (xmlNode *n = root->children; n; n = n->next) {
        if (named(n, "save-folder"))
            c->saveFolder = text(n);
        else if (named(n, "file-name"))
            c->fileNamePattern = text(n);
        else if (named(n, "copy-on-capture"))
            c->copyOnCapture = boolean(n);
        else if (named(n, "save-on-capture"))
            c->saveOnCapture = boolean(n);
        else if (named(n, "pointer"))
            c->pointer = boolean(n);
        else if (named(n, "colours")) {
            for (xmlNode *k = n->children; k; k = k->next) {
                // The colours are xs:token: leading and trailing whitespace is not part of the value.
                if (named(k, "pen"))
                    c->colours.pen = text(k).simplified();
                else if (named(k, "highlighter"))
                    c->colours.highlighter = text(k).simplified();
                else if (named(k, "shape"))
                    c->colours.shape = text(k).simplified();
                else if (named(k, "text"))
                    c->colours.text = text(k).simplified();
                else if (named(k, "marker"))
                    c->colours.marker = text(k).simplified();
            }
        }
    }
}

// Validates bytes against the schema and, when they validate, applies them to *config. Returns the error, or "".
QString parse(const QByteArray &bytes, const QString &path, Config *config)
{
    QStringList messages;
    const QByteArray xsd = Config::schema();
    if (xsd.isEmpty())
        return QStringLiteral("the schema is missing from the program");

    xmlSchemaParserCtxtPtr schemaParser = xmlSchemaNewMemParserCtxt(xsd.constData(), int(xsd.size()));
    if (!schemaParser)
        return QStringLiteral("cannot read the schema");
    xmlSchemaSetParserStructuredErrors(schemaParser, collect, &messages);
    xmlSchemaPtr schema = xmlSchemaParse(schemaParser);
    xmlSchemaFreeParserCtxt(schemaParser);
    if (!schema)
        return QStringLiteral("the schema does not load: %1").arg(messages.join(QStringLiteral("; ")));

    QString result;
    xmlParserCtxtPtr parser = xmlNewParserCtxt();
    if (!parser) {
        xmlSchemaFree(schema);
        return QStringLiteral("out of memory");
    }
    xmlCtxtSetErrorHandler(parser, collect, &messages);
    const QByteArray url = QFile::encodeName(path);
    // No network, no DTD loading, no entity expansion.
    xmlDocPtr doc = xmlCtxtReadMemory(parser, bytes.constData(), int(bytes.size()), url.constData(), nullptr,
                                      XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    xmlFreeParserCtxt(parser);
    if (!doc) {
        result = QStringLiteral("%1 is not well-formed XML: %2")
                     .arg(path, messages.isEmpty() ? QStringLiteral("unreadable") : messages.first());
    } else {
        xmlSchemaValidCtxtPtr validator = xmlSchemaNewValidCtxt(schema);
        if (!validator) {
            result = QStringLiteral("out of memory");
        } else {
            xmlSchemaSetValidStructuredErrors(validator, collect, &messages);
            const int rc = xmlSchemaValidateDoc(validator, doc);
            xmlSchemaFreeValidCtxt(validator);
            if (rc != 0) {
                result = QStringLiteral("%1 does not match the config schema: %2")
                             .arg(path, messages.isEmpty() ? QStringLiteral("invalid") : messages.join("; "));
            } else if (xmlNode *root = xmlDocGetRootElement(doc)) {
                apply(root, config);
            }
        }
        xmlFreeDoc(doc);
    }
    xmlSchemaFree(schema);
    return result;
}

QString boolText(bool b)
{
    return b ? QStringLiteral("true") : QStringLiteral("false");
}

} // namespace

QString Config::resolvedSaveFolder(const QString &home) const
{
    if (saveFolder == QLatin1String("~"))
        return home;
    if (saveFolder.startsWith(QLatin1String("~/")))
        return home + saveFolder.mid(1);
    return saveFolder;
}

QString Config::defaultPath()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = qEnvironmentVariable("HOME") + QStringLiteral("/.config");
    return base + QStringLiteral("/riley-shot/config.xml");
}

bool Config::load(const QString &path, Config *config, QString *error)
{
    if (error)
        error->clear();
    Config loaded;
    if (!QFileInfo::exists(path)) {
        if (config)
            *config = loaded;
        return true;
    }
    QFile file(path);
    QString why;
    if (!file.open(QIODevice::ReadOnly))
        why = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
    else
        why = parse(file.readAll(), path, &loaded);
    if (!why.isEmpty()) {
        if (config)
            *config = Config();
        if (error)
            *error = why;
        return false;
    }
    if (config)
        *config = loaded;
    return true;
}

bool Config::save(const QString &path, QString *error) const
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    const QString folder = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder))
        return fail(QStringLiteral("cannot create the folder %1").arg(folder));

    QByteArray bytes;
    {
        QXmlStreamWriter w(&bytes);
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(2);
        w.writeStartDocument();
        w.writeDefaultNamespace(QString::fromLatin1(kNamespace));
        w.writeStartElement(QStringLiteral("riley-shot"));
        w.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
        w.writeTextElement(QStringLiteral("save-folder"), saveFolder);
        w.writeTextElement(QStringLiteral("file-name"), fileNamePattern);
        w.writeTextElement(QStringLiteral("copy-on-capture"), boolText(copyOnCapture));
        w.writeTextElement(QStringLiteral("save-on-capture"), boolText(saveOnCapture));
        w.writeTextElement(QStringLiteral("pointer"), boolText(pointer));
        w.writeStartElement(QStringLiteral("colours"));
        w.writeTextElement(QStringLiteral("pen"), colours.pen);
        w.writeTextElement(QStringLiteral("highlighter"), colours.highlighter);
        w.writeTextElement(QStringLiteral("shape"), colours.shape);
        w.writeTextElement(QStringLiteral("text"), colours.text);
        w.writeTextElement(QStringLiteral("marker"), colours.marker);
        w.writeEndElement();
        w.writeEndElement();
        w.writeEndDocument();
        if (w.hasError())
            return fail(QStringLiteral("cannot write the config as XML"));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    file.write(bytes);
    if (!file.commit())
        return fail(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    if (error)
        error->clear();
    return true;
}

QByteArray Config::schema()
{
    QFile file(QStringLiteral(":/riley-shot/config.xsd"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace rileyshot
