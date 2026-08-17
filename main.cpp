/* main.cpp
 * Copyright 2022-2024 AutoZone, Inc.
 * Content is confidential to and proprietary information of AutoZone, Inc., its
 * subsidiaries and affiliates.
 */
// PROJECT INCLUDES
#include "commandlineparser.h"
#include "commznetapp.h"
#include "settings.h"
#include "signalwatcher.h"
#include "znetprocess.h"
#include "znetutils.h"

// ZNETPORT INCLUDES
#include "version.h"
#include "employeedata.h"

// ZNET INCLUDES
#include "znet.h"
#include "znetNS.h"
#include "znetGlobal.h"
#include "znetConstants.h"
#include "configEditorDialog.h"

// GEMINI INCLUDES
#include "busConnection.h"

// QT INCLUDES
#include <QApplication>
#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringList>

// SYSTEM INCLUDES
#include <csignal>
#include <sys/types.h>
#include <unistd.h>

// LOCAL CONSTANTS
namespace  {
const char* const  SCALING_ENV_VAR  { "QT_SCALE_FACTOR" };
}

// GLOBALS (no comment)
znetDefineGlobal( additionalPaths, "Additional Paths to set", "" );
znetDefineGlobal( allowBrowserPlugins, "Allow webkit to load plugins (flash)", true );
znetDefineGlobal( allowLogging, "If true, logging is send to the system output stream", false );
znetDefineGlobal( allowMultipleX11Sessions, "Allow multiple Znet instances to run on X11", false );
znetDefineGlobal( alpha3CountryCode, "3 letter country code", "USA" );
znetDefineGlobal( asyncPort, "The asynchronous port on the local store", 12016 );
znetDefineGlobal( azApplicationName, "application name for security purposes for web service requests", "ZNET" );
znetDefineGlobal( azCredentials, "credentials for security purposes for web service requests", "ISSDEV:password" );
znetDefineGlobal( bopusAllowed, "readOnly:BOPUS processing is allowed", false );
znetDefineGlobal( browserPluginPath, "Where to find plugins", "." );
znetDefineGlobal( callCenterMode, "Call center mode (Either Non Call Center, Commercial, or Roll Call)", znet::NonCallCenter );
znetDefineGlobal( centralizedPricingAvailable, "Centralized pricing is available", false );
znetDefineGlobal( checkoutAllowed, "readOnly:Checkout is allowed", false );
znetDefineGlobal( commercialPinpadEnabled, "Swipe/insert functionality for commercial enabled", "false" );
znetDefineGlobal( commercialReceiptEnabled, "Card receipt printing functionality for commercial enabled", "false" );
znetDefineGlobal( commercialStoreFunctionsResource, "Resource file accessor for commercial storefunctions js files", ":/storeFunctionsCommercialScriptfiles" );
znetDefineGlobal( commercialWirelessTerminal, "Wireless bank terminal for MX", "false" );
znetDefineGlobal( comStyleSheet, "Commercial stylesheet", ":/znet/commercial_style.qss" );
znetDefineGlobal( contentServer, "Content Server hostname", "znet1.autozone.com" );
znetDefineGlobal( contentServerBaseUrl, "URL for content server requests", "http://znet1.autozone.com:24999/znetcs" );
znetDefineGlobal( country, "For Tax Calculation purpose", "US" );
znetDefineGlobal( csPort, "Content Server Port", 24999 );
znetDefineGlobal( defaultFeatureMessage, "Default message for the featured result widget header", "AutoZone Recommends" );
znetDefineGlobal( defaultFeatureMessageId, "Default message id for the featured result widget header", "0" );
znetDefineGlobal( defaultMessageLocale, "Default locale for the messages, needed for translation process", "en_US" );
znetDefineGlobal( defaultNonFeatureMessage, "Default message for the related widget header", "Recommended" );
znetDefineGlobal( defaultNonFeatureMessageId, "Default message id for the related widget header", "0" );
znetDefineGlobal( defaultRequiredPartsMessage, "Default message for the related widgets required header", "Required" );
znetDefineGlobal( defaultRequiredPartsMessageId, "Default message id for the related widgets required header", "0" );
znetDefineGlobal( devTTY, "Store TTY Assignment", "99" );
znetDefineGlobal( diyStoreFunctionsResource, "Resource file accessor for diy storefunctions js files", ":/storeFunctionsDIYScriptfiles" );
znetDefineGlobal( emailAllowed, "readOnly:Email notification option is allowed", false );
znetDefineGlobal( expeditedPickingEnabled, "Y indicates that this store is it's own companion store(for area6)", false );
znetDefineGlobal( geminiPort, "Port used with the gemini service", 12100 );
znetDefineGlobal( geminiPortSSL, "Port used with the gemini service", 12101 );
znetDefineGlobal( iniFile, "readOnly:Application INI location", znet::znetIniLocation() );
znetDefineGlobal( isArea6Store, "Y indicates that this store has area6 items in stock", false );
znetDefineGlobal( isCommercialCashDrawerOn, "Is the Commercial Cash Drawer functionality enabled", false );
znetDefineGlobal( isCommercialRegister, "This is the Commercial Cash Register", false );
znetDefineGlobal( isDynamicallySlottedStore, "Y indicates that this store has dynamically slotted inventory", false );
znetDefineGlobal( localeTerritory, "Accomodates AZ's broken locale settings", "US" );
znetDefineGlobal( maxSearchHistoryRecords, "The maximum number of records in the ZNet Search History table", 3000 );
znetDefineGlobal( nexPartUrl, "URL for nexPart site", "https://www.nexpart.com" );
znetDefineGlobal( overrideCompetitorsThreshold, "Above it, price override competitors list is goign to be shown in a combo box", 10 );
znetDefineGlobal( overrideRequestTimeOut, "Time out in mili seconds for the price override service call", 5000 );
znetDefineGlobal( paidoutsTimeout, "Timeout in minutes for the paidouts plugin", 5 );
znetDefineGlobal( productInfoServerBaseUrl, "URL for productInfo requests", "http://znet1.autozone.com:24993/znetpis" );
znetDefineGlobal( registerNumber, "Register number of current device", "" );
znetDefineGlobal( registerWithTill, "Working device is a register and has a till", false );
znetDefineGlobal( remoteBuyAllowed, "readOnly:Remote Buy is allowed", false );
znetDefineGlobal( resourceDirectory, "Directory for dynamically loaded resource files", "/opt/znet/resources" );
znetDefineGlobal( salesTipTimeout, "salestip balloon widget timeout, in milliseconds", 3000 );
znetDefineGlobal( scanningAllowed, "readOnly:Item Scanning is allowed", false );
znetDefineGlobal( scriptsDirectory, "Plugin Scripts directory", "/opt/znet/scriptfiles" );
znetDefineGlobal( smsAllowed, "readOnly:SMS (txt msg) notification option is allowed", false );
znetDefineGlobal( smsAsyncPort, "Asynchronous port for the sms database", 12014 );
znetDefineGlobal( smsSyncPort, "Synchronous port for the sms database", 12015 );
znetDefineGlobal( sscMode, "True indicates this is an SSC client", false );
znetDefineGlobal( storeMerchantNo, "Value of kstore.merchant_no", "" );
znetDefineGlobal( syncPort, "The synchronous port on the local store", 12017 );
znetDefineGlobal( troubleCodeBaseUrl, "URL For trouble codes", "http:p-znet-app01.autozone.com:24989/troublecode" );
znetDefineGlobal( useProxy, "Whether to force proxy usage for non-local addresses", true );
znetDefineGlobal( webServicePort, "The web service port on the local store", 8080 );
znetDefineGlobal( zHeaderGray, "Default color for related part widget when orange", "#0d0" );
znetDefineGlobal( zHeaderOrange, "Default color for related part widget when orange", "#FF8000" );
znetDefineGlobal( zposDatabase, "zpos Database", "zpos" );
znetDefineGlobal( zposPort, "Port for talking to zpos", 12012 );
znetDefineGlobal( zposSyncPort, "Port for talking to zpos", 12013 );
znetDefineGlobal( commZnet, "readOnly:Flag to identify commZnet is active", true );


//----------------------------------------------------------------------------

static QString  znetVersion()
{
    return  QString( "CommZnet Version %1.%2.%3" )
            .arg( MAJOR ).arg( MINOR ).arg( RELEASE );
}

//----------------------------------------------------------------------------

static void  loadConfiguration()
{
    if (!QFile::exists( znetGlobal["iniFile"].toString() ))
    {
        qWarning() << "No INI file exists... creating a new one";
        configEditorDialog::resetINI( znetGlobal["iniFile"].toString() );
    }
    else
    {
        configEditorDialog::reloadINI( znetGlobal["iniFile"].toString() );
        qDebug() << "Globals after ini file loaded";
        znet::listGlobals();
    }
}

//----------------------------------------------------------------------------
// Modification History:
// MM/DD/CCYY	NAME		DESCRIPTION
// 07/19/2024   K.Sheffield STEX-4731 Set initial value for alpha3CountryCode

static void  setLocales( const QLocale& oldLocale, const QLocale& newLocale )
{
    qDebug() << "Locale set (new):" << newLocale.name();
    qDebug() << "Locale set (old):" << oldLocale.name();

    znet::setLocale( oldLocale.name() );
    znetGlobal["defaultMessageLocale"] = oldLocale.name();

    if (newLocale.country() == QLocale::PuertoRico)
    {
        znetGlobal["localeTerritory"] = "PR";
        znetGlobal["country"] = "US";
    }
    else if (newLocale.country() == QLocale::Mexico)
    {
        znetGlobal["localeTerritory"] = "MX";
        znetGlobal["country"] = "MX";
    }
    else if (newLocale.country() == QLocale::Brazil)
    {
        znetGlobal["localeTerritory"] = "BR";
        znetGlobal["secondaryLocale"] = "pt_BR";
        znetGlobal["country"] = "BR";
    }
    else
    {
        znetGlobal["localeTerritory"] = "US";
        znetGlobal["country"] = "US";
    }
    // setup the initial version
    znetGlobal["alpha3CountryCode"] =
      znet::countryAlpha2to3(znetGlobal["country"].toString());
}

//----------------------------------------------------------------------------

int loadDefaultLocale()
{
  int retVal = false;
  geminiQuery localeq;
  /* Uses default "storeServer" and "geminiPort", not country specific */
  localeq.setHost(znetGlobal["storeServer"].toString(),
                  znetGlobal["geminiPort"].toInt());
  localeq.setMethod("ezcDal::getDefaultLocale");
  if(localeq.query(true) && localeq.resultCount()>0)
    {
      stringHash t = localeq.result().first();

      znet::setLocale(t["locale"]);
      qDebug()<<"t[locale]="<<t["locale"];
      znetGlobal["primaryLocale"] = t["locale"];
      znetGlobal["secondaryLocale"] = t["secondaryLocale"];
      znetGlobal["country"] = t["country"];
      znetGlobal["localeTerritory"] = t["country"];
      znetGlobal["currencyCode"] = t["currencyCode"];
      znetGlobal["charsetID"] = t["charsetID"];
      znetGlobal["countryState"] = t["countryState"];

      retVal = true;

      /* Puerto Rico is handled differently and MUST check country and
       * countryState - PR is used in other countries (Parana in Brazil)!!! */
      if(t["country"] == "US" && t["countryState"] == "PR")
        {
          znetGlobal["localeTerritory"] = "PR"; // sigh
        }
      qDebug() << __PRETTY_FUNCTION__ << "localeq success!" 
               << "country:" << znetGlobal["country"].toString();
    }
  else
    {
      qDebug() << __PRETTY_FUNCTION__ << "localeq failed!";
      znetGlobal["country"] = "US";
      znet::setLocale("en_US");
    }

  return retVal;
}


//============================================================================
//============================================================================

//----------------------------------------------------------------------------

int  main( int argc, char* argv[] )
{
    CommandLineParser  parser( argc, argv );

    if (parser.hasVersion())
    {
        QTextStream  outstrm( stdout );
        outstrm << znetVersion() << "\n";
        return  0;
    }

    Settings  settings;

    znet::znetGlobalInit();
    znet::addTranslation( QStringLiteral( "zCoreWidgets" ) );
    znet::addTranslation( QStringLiteral( "znet" ) );

    qInstallMessageHandler( znet::getLogOutput() );

    qDebug() << "Globals before ini file loaded";
    znet::listGlobals();
    loadConfiguration();
    znet::loadGlobalsFromEnvironment();

    znet::setAllowLogging( znetGlobal["allowLogging"].toBool() );
    busConnection::localDefaultSettings().setObjectName( QStringLiteral( "local" ) );
    busConnection::remoteDefaultSettings().setObjectName( QStringLiteral( "lan" ) );
    znet::setPropertiesFromGlobals( busConnection::localDefaultSettings() );
    znet::setPropertiesFromGlobals( busConnection::remoteDefaultSettings() );

    if (parser.hasVerbose() || settings.isVerbose())
    {
        znet::setAllowLogging( true );
    }

    if (parser.hasServer())
    {
        znetGlobal["storeServer"] = parser.server();
        znetGlobal["proxyServer"] = parser.server();
    }

    if (parser.hasExportLookups())
    {
        return  ZnetUtils::exportLookups();
    }
    else if (parser.hasDownloadResources())
    {
        return  ZnetUtils::downloadResources( argc, argv );
    }

    znetGlobal["managerTerm"] = parser.hasManagerTerminal() ? "true" : "false";

    if (parser.hasScaling())
    {
        qputenv( SCALING_ENV_VAR, parser.scaling().toLatin1() );
    }

    bool  dologin = true;
    EmployeeData  empdata;
    if (parser.hasEmployeeData())
    {
        empdata = EmployeeData::fromEncoded( parser.employeeData(), QString() );
        if (empdata.isValid())
        {
            dologin = false;
            qDebug() << "Commercial employee:" << empdata.name();
        }
    }

    if (dologin)
    {
        ///\todo Request login if commznet needs to support independent startup
        qCritical() << "No employee defined!";
        return  1;
    }

    QString  startDirectory = QDir::currentPath();

    // Prepare GUI
    QApplication::addLibraryPath( znetGlobal["zPluginPath"].toString() );
    QApplication::setAttribute( Qt::AA_ShareOpenGLContexts, true );

    qInfo() << "Creating CommZnetApp now....";

    CommZnetApp  app( argc, argv );
    app.setApplicationName( QStringLiteral( "commznet" ) );
    app.setApplicationVersion( znetVersion() );
    app.setEmployeeData( empdata );
    app.setProperty( "__commznet__", true );

    // Signal handling
    setpgid( 0, 0 );    // become the process group leader so cleanup
                        // does not kill unrelated processes
    SignalWatcher  sigwatch;
    QObject::connect( &sigwatch, &SignalWatcher::signalEvent,
                      &app,      &CommZnetApp::quit );
    sigwatch.addSignal( SIGHUP );
    sigwatch.addSignal( SIGINT );
    sigwatch.addSignal( SIGTERM );

    znet::setCommercialFlags();

    if (parser.hasLocales())
    {
        if(loadDefaultLocale() == false)
          {
            setLocales( parser.legacyLocale(), parser.locale() );
          }
        app.setDefaultLocale( znet::locale() );
    }
    else
    {
        znet::setLocale( QStringLiteral( "en_US" ) );
        qWarning() << "CommZnet not initiated by Hybrid Z-net";
    }

    app.configure();

    if (!znetGlobal["additionalPaths"].toByteArray().isEmpty())
    {
        QByteArray  path = qgetenv( "PATH" );
        path += znetGlobal["additionalPaths"].toByteArray();
        qputenv( "PATH", path );
        qDebug() << "Path is now set to" << path;
    }

    if (settings.isTerminateZnetEnabled() && parser.hasZnetPid())
    {
        if (!ZnetProcess::terminate( parser.znetPid() ))
        {
            qDebug() << "Failed to terminate DIY Znet";
        }
    }

    if (app.exec() == -1)
    {
        qDebug() << "Restarting...\n";
        QProcess::startDetached( argv[0], app.arguments(), startDirectory );
        return  0;
    }

    qDebug() << "Commercial Z-net exiting...";

    return  0;
}
