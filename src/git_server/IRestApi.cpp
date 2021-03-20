#include <IRestApi.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

using namespace GitServer;

IRestApi::IRestApi(const ServerAuthentication &auth, QObject *parent)
   : QObject(parent)
   , mManager(new QNetworkAccessManager())
   , mAuth(auth)
{
}

IRestApi::~IRestApi()
{
   delete mManager;
}

QJsonDocument IRestApi::validateData(QNetworkReply *reply, QString &errorString)
{
   const auto data = reply->readAll();
   const auto jsonDoc = QJsonDocument::fromJson(data);

   if (reply->error() != QNetworkReply::NoError)
   {
      emit log(QString("Error #%1 - %2.").arg(QString::number(reply->error()), reply->errorString()));

      errorString = reply->errorString();

      return QJsonDocument();
   }

   if (jsonDoc.isNull())
   {
      emit log(QString("Error when parsing Json. Current data:\n%1").arg(QString::fromUtf8(data)));
      return QJsonDocument();
   }

   const auto jsonObject = jsonDoc.object();
   if (jsonObject.contains(QStringLiteral("message")))
   {
      const auto message = jsonObject[QStringLiteral("message")].toString();
      QString details;

      if (jsonObject.contains(QStringLiteral("errors")))
      {
         const auto errors = jsonObject[QStringLiteral("errors")].toArray();

         for (const auto &error : errors)
            details = error[QStringLiteral("message")].toString();

         errorString = message + ". " + details;

         emit log(errorString);

         return QJsonDocument();
      }
   }
   else if (jsonObject.contains(QStringLiteral("error")))
   {
      errorString = jsonObject[QStringLiteral("error")].toString();

      emit log(errorString);

      return QJsonDocument();
   }

   reply->deleteLater();

   return jsonDoc;
}
