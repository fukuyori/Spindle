#include "epub/ZipArchive.h"

#include "miniz.h"

#include <cstring>

struct ZipArchive::Impl {
    mz_zip_archive zip{};
};

ZipArchive::~ZipArchive()
{
    close();
}

bool ZipArchive::open(const QString &filePath)
{
    close();
    m_d = new Impl();
    std::memset(&m_d->zip, 0, sizeof(m_d->zip));
    const QByteArray utf8 = filePath.toUtf8();
    if (!mz_zip_reader_init_file(&m_d->zip, utf8.constData(), 0)) {
        delete m_d;
        m_d = nullptr;
        return false;
    }
    m_open = true;
    return true;
}

void ZipArchive::close()
{
    if (m_d) {
        if (m_open)
            mz_zip_reader_end(&m_d->zip);
        delete m_d;
        m_d = nullptr;
    }
    m_open = false;
}

bool ZipArchive::contains(const QString &path) const
{
    if (!m_open)
        return false;
    const QByteArray utf8 = path.toUtf8();
    return mz_zip_reader_locate_file(&m_d->zip, utf8.constData(), nullptr, 0) >= 0;
}

QByteArray ZipArchive::readBytes(const QString &path) const
{
    if (!m_open)
        return {};
    const QByteArray utf8 = path.toUtf8();
    int index = mz_zip_reader_locate_file(&m_d->zip, utf8.constData(), nullptr, 0);
    if (index < 0)
        return {};

    size_t size = 0;
    void *data = mz_zip_reader_extract_to_heap(&m_d->zip, static_cast<mz_uint>(index), &size, 0);
    if (!data)
        return {};
    QByteArray out(static_cast<const char *>(data), static_cast<int>(size));
    mz_free(data);
    return out;
}

QString ZipArchive::readText(const QString &path) const
{
    return QString::fromUtf8(readBytes(path));
}

QStringList ZipArchive::entries() const
{
    QStringList out;
    if (!m_open)
        return out;
    const mz_uint count = mz_zip_reader_get_num_files(&m_d->zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&m_d->zip, i))
            continue;
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&m_d->zip, i, &stat))
            continue;
        out.append(QString::fromUtf8(stat.m_filename));
    }
    return out;
}
