/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * https://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBREPCB_CORE_NETWORKREQUESTBASE_H
#define LIBREPCB_CORE_NETWORKREQUESTBASE_H

/*******************************************************************************
 *  Includes
 ******************************************************************************/
#include <QtCore>
#include <QtNetwork>

#include <memory>

/*******************************************************************************
 *  Namespace / Forward Declarations
 ******************************************************************************/
namespace librepcb {

/*******************************************************************************
 *  Class NetworkRequestBase
 ******************************************************************************/

/**
 * @brief Base class for network requests which are processed in the network
 * access manager
 *
 * This class lets you execute a network request without blocking the main
 * application thread. After creating an object derived from
 * ::librepcb::NetworkRequestBase, you can connect to signals of that class to
 * track the progress of the request. Then you need to call #start() to start
 * the request processing.
 *
 * @note    You need to ensure that an instance of
 * librepcb::NetworkAccessManager exists while starting a new network request.
 * Otherwise the request will fail. Read the documentation of
 * librepcb::NetworkAccessManager for more information.
 *
 * @see librepcb::NetworkAccessManager
 */
class NetworkRequestBase : public QObject {
  Q_OBJECT

public:
  // Constructors / Destructor
  NetworkRequestBase() = delete;
  NetworkRequestBase(const NetworkRequestBase& other) = delete;
  NetworkRequestBase(const QUrl& url,
                     const QByteArray& postData = QByteArray()) noexcept;
  virtual ~NetworkRequestBase() noexcept;

  // Setters

  /**
   * @brief Set a HTTP header field for the network request
   *
   * @param name      Header field name
   * @param value     Header field value
   */
  void setHeaderField(const QByteArray& name, const QByteArray& value) noexcept;

  /**
   * @brief Set the cache load control attribute
   *
   * Allows to control the caching behavior, e.g. enforcing download only from
   * cache but not from the network. For details, see documentation of
   * `QNetworkRequest::setAttribute()`.
   *
   * @param value   The new cache control value
   */
  void setCacheLoadControl(QNetworkRequest::CacheLoadControl value) noexcept;

  /**
   * @brief Set the expected size of the requested content
   *
   * If set, this size will be used to calculate the download progress in
   * percent in case that there is no "Content-Length" attribute in the received
   * HTTP header.
   *
   * @param bytes         Expected content size of the reply in bytes
   */
  void setExpectedReplyContentSize(qint64 bytes) noexcept;

  /**
   * @brief Set the minimum time the request should be cached
   *
   * This allows to cache requests longer than specified in response headers,
   * which is useful for URLs not under our own control where rather uncritical
   * content like images are downloaded from. If the response header specifies
   * a higher max-age value, it has priority so this method has no effect.
   *
   * @param seconds       Minimum cache time in seconds (default is 0)
   */
  void setMinimumCacheTime(int seconds) noexcept;

  /**
   * @brief Use a typical browser user agent for this request
   *
   * It turned out at least st.com blocks downloading files if the request
   * is not coming from a browser. For downloading datasheets we don't care
   * about the user agent so let's just work arount this issue by using
   * a well-known user agent.
   */
  void useBrowserUserAgent() noexcept;

  // Operator Overloadings
  NetworkRequestBase& operator=(const NetworkRequestBase& rhs) = delete;

public slots:

  /**
   * @brief Start downloading the requested content
   *
   * @warning It is not save to access this object after calling this method!
   *          The object will be moved to another thread and will be deleted
   * after an error occurs or the request succeeds. Any further access to the
   *          pointer which you have received from the constructor is unsave and
   *          could cause an application crash.
   */
  void start() noexcept;

  /**
   * @brief Abort downloading the requested content
   *
   * @warning Because calling this method makes only sense *after* calling
   * #start(), but which is unsave as described in #start(), this method must
   * only be used indirectly with the signals/slots concept of Qt (Qt
   * automatically disconnects the callers signal from this slot as soon as this
   * object gets destroyed, so the connection is always safe).
   */
  void abort() noexcept;

signals:

  /**
   * @brief Internal signal, don't use it from outside
   */
  void startRequested();

  /**
   * @brief Reply progress / state changed signal
   *
   * This signal shows which actions are executed. Or in other words, it shows
   * the current state of the request processing.
   *
   * @param state       Short description about the current action/state
   */
  void progressState(QString state);

  /**
   * @brief Reply content download progress signal (simple)
   *
   * @param percent               (Estimated) progress in percent (0..100)
   */
  void progressPercent(int percent);

  /**
   * @brief Reply content download progress signal (extended)
   *
   * @param bytesReceived         Count of bytes received
   * @param bytesTotal            Count of total bytes (-1 if unknown)
   * @param percent               (Estimated) progress in percent (0..100)
   */
  void progress(qint64 bytesReceived, qint64 bytesTotal, int percent);

  /**
   * @brief Request aborted signal (emitted right before #finished())
   */
  void aborted();

  /**
   * @brief Request succeeded signal (emitted right before #finished())
   */
  void succeeded();

  /**
   * @brief Request errored signal (emitted right before #finished())
   *
   * @param errorMsg              An error message
   */
  void errored(QString errorMsg);

  /**
   * @brief Request finished signal
   *
   * This signal is emitted right after #aborted(), #succeeded() or #errored().
   *
   * @param success               True if succeeded, false if aborted or errored
   */
  void finished(bool success);

public:  // Methods
  virtual void prepareRequest() = 0;
  virtual void finalizeRequest() = 0;
  virtual void emitSuccessfullyFinishedSignals(
      QString contentType) noexcept = 0;
  virtual void fetchNewData(QIODevice& device) noexcept = 0;

private:  // Methods
  void executeRequest() noexcept;
  void uploadProgressSlot(qint64 bytesSent, qint64 bytesTotal) noexcept;
  void replyDownloadProgressSlot(qint64 bytesReceived,
                                 qint64 bytesTotal) noexcept;
  void replyReadyReadSlot() noexcept;
  void replyErrorSlot(QNetworkReply::NetworkError code) noexcept;
  void replySslErrorsSlot(const QList<QSslError>& errors) noexcept;
  void replyFinishedSlot() noexcept;
  void finalize(const QString& errorMsg,
                const QString& contentType = QString()) noexcept;
  static QString formatFileSize(qint64 bytes) noexcept;
  static QString getUserAgent() noexcept;

protected:  // Data
  // from constructor
  QUrl mUrl;
  QByteArray mPostData;
  qint64 mExpectedContentSize;
  int mMinimumCacheTime;

  // internal data
  QList<QUrl> mRedirectedUrls;
  QNetworkRequest mRequest;
  std::unique_ptr<QNetworkReply> mReply;
  bool mStarted;
  bool mAborted;
  bool mErrored;
  bool mFinished;
};

/*******************************************************************************
 *  End of File
 ******************************************************************************/

}  // namespace librepcb

#endif
