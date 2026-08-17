/* greenscreen.cpp
 * Copyright 2021-2025 AutoZone, Inc.
 * Content is confidential to and proprietary information of AutoZone, Inc., its
 * subsidiaries and affiliates.
 */

// PROJECT INCLUDES
#include "greenscreen.h"
#include "terminal.h"
#include "znetcashdrawer.h"
#include "znetcommon.h"
#include "znetprinter.h"
#include "znetscanner.h"

// QT INCLUDES
#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QKeyEvent>
#include <QMap>
#include <QMessageBox>
#include <QString>
#include <QTcpSocket>
#include <QVariant>
#include <QVBoxLayout>

// SYSTEM INCLUDES
#include <arpa/inet.h>

// LOCAL CONSTANTS
namespace {
const char* const  ENV_CPOS_HOST     { "CPOSHOST" };
const QString  DEFAULT_COMM_HOST     { QStringLiteral( "10.20.30.10" ) };
const int      DEFAULT_COMM_PORT     { 12011 };
const int      DEFAULT_COMM_WAIT_MS  { 250 };
const QString  DEFAULT_EXEC_PATH     { QStringLiteral( "/user/sms/bin" ) };
const QString  DEFAULT_ARGS_SEP      { QStringLiteral( " " ) };
const QString  DEFAULT_APP_ARGS      { DEFAULT_ARGS_SEP };
const QString  SMS_EXEC_KENTWAR      { QStringLiteral( "kentwar.x" ) };
const QString  SMS_EXEC_PCR          { QStringLiteral( "pcr.x" ) };
const QString  SMS_EXEC_KEPASRCV     { QStringLiteral( "kepasrcv.x" ) };
const QString  SMS_EXEC_KEPAORDE     { QStringLiteral( "kepaorde.x" ) };
const QString  SMS_EXEC_KITMOVR      { QStringLiteral( "kitmovr.x" ) };
const QString  SMS_EXEC_PLRLOOKUP    { QStringLiteral( "plrlookup.x" ) };
const QString  SMS_EXEC_MNDRREPORT   { QStringLiteral( "mndrreport.x" ) };
const QString  SMS_EXEC_AUCNTRPT     { QStringLiteral( "aucntrpt.x" ) };
const QString  SMS_EXEC_PCISIGRPT    { QStringLiteral( "pcisigrpt.x" ) };
const QString  SMS_EXEC_INVPREMENU   { QStringLiteral( "invpremenu.x" ) };
const QString  SMS_EXEC_KPASSWD      { QStringLiteral( "kpasswd.x" ) };
const QString  SMS_EXEC_INVMENU      { QStringLiteral( "invmenu.x" ) };
const QString  SMS_EXEC_KEMPLO       { QStringLiteral( "kmemplo.x" ) };
const QString  SMS_EXEC_WITTF16MENU  { QStringLiteral( "wittf16menu.x" ) };
const QString  SMS_EXEC_KUOILMNU     { QStringLiteral( "kuoilmnu.x" ) };
const QString  SMS_EXEC_KUOILREFPAY  { QStringLiteral( "kuoilrefpay.x" ) };
const QString  SMS_EXEC_KBAT         { QStringLiteral( "kbat.x" ) };
const QString  SMS_EXEC_KLOGIN       { QStringLiteral( "klogin.x" ) };
const QString  SMS_EXEC_KCHORD       { QStringLiteral( "kchord.x" ) };
const QString  SMS_EXEC_OVERSTOCK    { QStringLiteral( "overstock.x" ) };
const QString  SMS_EXEC_KPCASH       { QStringLiteral( "kpcash.x" ) };
const QString  SMS_EXEC_LANEACCT     { QStringLiteral( "laneacct.x" ) };
const QString  SMS_EXEC_KGREGIS      { QStringLiteral( "kgregis.x" ) };
const QString  SMS_EXEC_RFDEPOSIT    { QStringLiteral( "rfdeposit.x" ) };
const QString  SMS_EXEC_DEPHIST      { QStringLiteral( "dephist.x" ) };
const QString  SMS_EXEC_KCASTIK      { QStringLiteral( "kcastik.x" ) };
const QString  SMS_EXEC_KSPRINT      { QStringLiteral( "ksprint.x" ) };
const QString  SMS_EXEC_KSA_MENU     { QStringLiteral( "ksa_menu.x" ) };
const QString  SMS_EXEC_DTMENU       { QStringLiteral( "dtmenu.x" ) };
const QString  SMS_EXEC_KDWACCT      { QStringLiteral( "kdwacct.x" ) };
const QString  SMS_EXEC_KDEFCRPT     { QStringLiteral( "kdefcrpt.x" ) };
const QString  SMS_EXEC_SIGNRPT      { QStringLiteral( "signrpt.x" ) };
const QString  SMS_EXEC_KOMOVRDRPT   { QStringLiteral( "kom_overide_report.x" ) };
const QString  SMS_EXEC_KELRCPT      { QStringLiteral( "kelrcpt.x" ) };
const QString  SMS_EXEC_IMASSIGNRT   { QStringLiteral( "im_assign_route.x" ) };
const QString  SMS_EXEC_IMMONDRV     { QStringLiteral( "immondrv.x" ) };
const QString  SMS_EXEC_KCLRRPT      { QStringLiteral( "kclrrpt.x" ) };
const QString  SMS_EXEC_INVBLD       { QStringLiteral( "invbld.x" ) };
const QString  SMS_EXEC_INVVIEWPRE   { QStringLiteral( "invviewpre.x" ) };
const QString  SMS_EXEC_INVTAG       { QStringLiteral( "invtag.x" ) };
const QString  SMS_EXEC_INVMISC      { QStringLiteral( "invmisc.x" ) };
const QString  SMS_EXEC_INVTEAM      { QStringLiteral( "invteam.x" ) };
const QString  SMS_EXEC_KPRETURNS    { QStringLiteral( "kpreturns.x" ) };
const QString  SMS_EXEC_KBATLOG      { QStringLiteral( "kbatlog.x" ) };
const QString  SMS_EXEC_KBATWS       { QStringLiteral( "kbatws.x" ) };
const QString  SMS_EXEC_KPMANRPT     { QStringLiteral( "kpmanrpt.x" ) };
const QString  SMS_EXEC_KPCIMOD      { QStringLiteral( "kpcimod.x" ) };
const QString  SMS_EXEC_KPMODRPT     { QStringLiteral( "kpmodrpt.x" ) };
const QString  SMS_EXEC_KPCIREP      { QStringLiteral( "kpcirep.x" ) };
const QString  SMS_EXEC_KSTDAMAGES   { QStringLiteral( "kstdamages.x" ) };
const QString  SMS_EXEC_KPCILKUP     { QStringLiteral( "kpcilkup.x" ) };
const QString  SMS_EXEC_AWARLKUP     { QStringLiteral( "awarlkup.x" ) };
const QString  SMS_EXEC_KQTYRPT      { QStringLiteral( "kqtyrpt.x" ) };
const QString  SMS_EXEC_KCCRPT       { QStringLiteral( "kccrpt.x" ) };
const QString  SMS_EXEC_KPRIBOOK     { QStringLiteral( "kpribook.x" ) };
const QString  SMS_EXEC_KDSTATUS     { QStringLiteral( "kdstatus.x" ) };
const QString  SMS_EXEC_KRITMPLN     { QStringLiteral( "kritmpln.x" ) };
const QString  SMS_EXEC_ADTRPTS      { QStringLiteral( "adtrpts.x" ) };
const QString  SMS_EXEC_LANEHISTRPT  { QStringLiteral( "lanehistrpt.x" ) };
const QString  SMS_EXEC_MASTERTRKRPT { QStringLiteral( "mastertrkrpt.x" ) };
const QString  SMS_EXEC_KMSYSRPT     { QStringLiteral( "kmsysrpt.x" ) };
const QString  SMS_EXEC_PUNCHRPT     { QStringLiteral( "punch_rpt.x" ) };
const QString  SMS_EXEC_WKLYLNRPT    { QStringLiteral( "wklylnrpt.x" ) };
const QString  SMS_EXEC_HLPDSK       { QStringLiteral( "hlpdsk.x" ) };
const QString  SMS_EXEC_SMSCLKEOD    { QStringLiteral( "smsclkeod.x" ) };
const QString  SMS_EXEC_SMSCLKEOW    { QStringLiteral( "smsclkeow.x" ) };
const QString  SMS_EXEC_INV2INV      { QStringLiteral( "inv2inv.x" ) };
const QString  SMS_EXEC_RPADJRPT     { QStringLiteral( "rpadjrpt.x" ) };
const QString  SMS_EXEC_ADJDETAIL    { QStringLiteral( "adjdetail.x" ) };
const QString  SMS_EXEC_NIMBLDRPT    { QStringLiteral( "nimbldrpt.x" ) };
const QString  SMS_EXEC_INMSELDATE   { QStringLiteral( "inmseldate.x" ) };
const QString  SMS_EXEC_KDBACKUP     { QStringLiteral( "kdbackup.x" ) };
const QString  SMS_EXEC_KEMPINFO     { QStringLiteral( "kempinfo.x" ) };
const QString  SMS_EXEC_HUBDLVRSCRN  { QStringLiteral( "hubdlvrscrn.x" ) };
const QString  SMS_EXEC_KPRTBADGE    { QStringLiteral( "kprtbadge.x" ) };
const QString  SMS_EXEC_KLSTORE      { QStringLiteral( "klstore.x" ) };
const QString  SMS_EXEC_KLEXEMPT     { QStringLiteral( "klexempt.x" ) };
const QString  SMS_EXEC_FLEETVIEW    { QStringLiteral( "fleetview.x" ) };
const QString  SMS_EXEC_RPT5PM       { QStringLiteral( "rpt5pm.x" ) };
const QString  SMS_EXEC_VDPREPT      { QStringLiteral( "vdprept.x" ) };
const QString  SMS_EXEC_KEPASPRINT   { QStringLiteral( "kepasprint.x" ) };
const QString  SMS_EXEC_KEPASDLVR    { QStringLiteral( "kepasdlvr.x" ) };
const QString  SMS_EXEC_PRTOUTSPO    { QStringLiteral( "prt_outs_po.x" ) };
const QString  SMS_EXEC_KEPASLOG     { QStringLiteral( "kepaslog.x" ) };
const QString  SMS_EXEC_KEXPCAN      { QStringLiteral( "kexpcan.x" ) };
const QString  SMS_EXEC_VDPSHIP      { QStringLiteral( "vdpship.x" ) };
const QString  SMS_EXEC_PRTVDPORD    { QStringLiteral( "prtvdpord.x" ) };
const QString  SMS_EXEC_KUTILITY     { QStringLiteral( "kutility.x" ) };
const QString  SMS_EXEC_IMORDMAG     { QStringLiteral( "imordmag.x" ) };
const QString  SMS_EXEC_IMVIEWORDRS  { QStringLiteral( "im_view_orders.x" ) };
const QString  SMS_EXEC_PEDIMENTO    { QStringLiteral( "impedrpt.x" ) };
const QString  SMS_EXEC_IMPRNTLBLS   { QStringLiteral( "imprntlabels.x" ) };
const QString  SMS_EXEC_IMPRTVIN     { QStringLiteral( "imprtvin.x" ) };
const QString  SMS_EXEC_IMREPMANIF   { QStringLiteral( "im_rep_manifest.x" ) };
const QString  SMS_EXEC_SLOTQOHVAR   { QStringLiteral( "slot_qoh_variance.x" ) };
const QString  SMS_EXEC_IMSHIPACK    { QStringLiteral( "im_shipment_ack.x" ) };
const QString  SMS_EXEC_KPO          { QStringLiteral( "kpo.x" ) };
const QString  SMS_EXEC_KCATLG       { QStringLiteral( "kcatlg.x" ) };
const QString  SMS_EXEC_DSDRPT       { QStringLiteral( "dsdrpt.x" ) };
const QString  SMS_EXEC_IMRECVING    { QStringLiteral( "im_receiving.x" ) };
const QString  SMS_EXEC_RSLOTVIEW    { QStringLiteral( "rslot_view.x" ) };
const QString  SMS_EXEC_RFDSDRC      { QStringLiteral( "rfdsdrc.x" ) };
const QString  SMS_EXEC_KRMISC       { QStringLiteral( "krmisc.x" ) };
const QString  SMS_EXEC_EPVIEW       { QStringLiteral( "epview.x" ) };
const QString  SMS_EXEC_PLRMENU      { QStringLiteral( "plrmenu.x" ) };
const QString  SMS_EXEC_IMDCACK      { QStringLiteral( "imdcack.x" ) };
const QString  SMS_EXEC_IMDCRCV      { QStringLiteral( "imdcrcv.x" ) };
const QString  SMS_EXEC_RFLOGOFF     { QStringLiteral( "rflogoff.x" ) };
const QString  SMS_EXEC_LCORDPRN     { QStringLiteral( "lcordprn.x" ) };
const QString  SMS_EXEC_KPLNEDT      { QStringLiteral( "kplnedt.x" ) };
const QString  SMS_EXEC_KFLRIDS      { QStringLiteral( "kflrids.x" ) };
const QString  SMS_EXEC_KFLRVER      { QStringLiteral( "kflrver.x" ) };
const QString  SMS_EXEC_LOCPOGRPT    { QStringLiteral( "locpogrpt.x" ) };
const QString  SMS_EXEC_RCMENU       { QStringLiteral( "rcmenu.x" ) };
const QString  SMS_EXEC_RCSTORE      { QStringLiteral( "rcstore.x" ) };
const QString  SMS_EXEC_KIADJUST     { QStringLiteral( "kiadjust.x" ) };
const QString  SMS_EXEC_KDCYCCNT     { QStringLiteral( "kdcyccnt.x" ) };
const QString  SMS_EXEC_KDCYCLE      { QStringLiteral( "kdcycle.x" ) };
const QString  SMS_EXEC_KPLNUTIL     { QStringLiteral( "kplnutil.x" ) };
const QString  SMS_EXEC_NOPOGRPT     { QStringLiteral( "nopogrpt.x" ) };
const QString  SMS_EXEC_PRCHGMENU    { QStringLiteral( "prchg_menu.x" ) };
const QString  SMS_EXEC_LBLMENU      { QStringLiteral( "lblmenu.x" ) };
const QString  SMS_EXEC_KSKUBARCODLS { QStringLiteral( "kskubarcodlst.x" ) };
const QString  SMS_EXEC_RFDSD        { QStringLiteral( "rf_dsd.x" ) };
const QString  SMS_EXEC_IMRCVDP      { QStringLiteral( "imrcvdp.x" ) };
const QString  SMS_EXEC_KPRTPOG      { QStringLiteral( "kprtpog.x" ) };
const QString  SMS_EXEC_DLVTASKS     { QStringLiteral( "dlvtasks.x" ) };//END
const QString  SMS_EXEC_ORDCMPLTN    { QStringLiteral( "ordcmpltn.x" ) };   // Commercial only ???
const QString  SMS_EXEC_ORDRMGMT     { QStringLiteral( "ordrmgmt.x" ) };
const QString  SMS_EXEC_KAUTOHOUR    { QStringLiteral( "kautohour.x" ) };
const QString  SMS_EXEC_OMRETURNS    { QStringLiteral( "omreturns.x" ) };
const QString  SMS_EXEC_KDRVRTRK     { QStringLiteral( "kdrvrtrk.x" ) };
const QString  SMS_EXEC_KEPO         { QStringLiteral( "kepo.x" ) };
const QString  SMS_EXEC_KSCANINVSIG  { QStringLiteral( "kscaninvsig.x" ) };
const QString  SMS_EXEC_AZCLKUTIL    { QStringLiteral( "azclkutil.x" ) };
const QString  SMS_EXEC_CLAIMS       { QStringLiteral( "claims.x" ) };
const QString  SMS_EXEC_KREPRINT     { QStringLiteral( "kreprint.x" ) };
const QString  SMS_EXEC_CUSTINV      { QStringLiteral( "custinv.x" ) };
const QString  SMS_EXEC_KAZODLY      { QStringLiteral( "kazodly.x" ) };
const QString  SMS_EXEC_EXPCCRPT     { QStringLiteral( "expccrpt.x" ) };
const QString  SMS_EXEC_KRFMENU      { QStringLiteral( "krfmenu.x" ) };
const QString  SMS_EXEC_KRPTUTIL     { QStringLiteral( "krptutil.x" ) };
const QString  SMS_EXEC_IMPICKERRPT  { QStringLiteral( "im_picker_rpt.x" ) };
const QString  SMS_EXEC_IMDRPT       { QStringLiteral( "imdrpt.x" ) };
const QString  SMS_EXEC_IMSATRPT     { QStringLiteral( "imsatrpt.x" ) };
const QString  SMS_EXEC_IMHUBRPT     { QStringLiteral( "im_hub_rpt.x" ) };
const QString  SMS_EXEC_IM_QC_RPT    { QStringLiteral( "im_qc_rpt.x" ) };
const QString  SMS_EXEC_IMSKIPSKU    { QStringLiteral( "imskipsku.x" ) };
const QString  SMS_EXEC_IMQCRPT      { QStringLiteral( "imqcrpt.x" ) };
const QString  SMS_EXEC_IMRCVRPT     { QStringLiteral( "imrcvrpt.x" ) };
const QString  SMS_EXEC_IMDSDRPT     { QStringLiteral( "imdsdrpt.x" ) };
const QString  SMS_EXEC_IMVIEWTRANSF { QStringLiteral( "im_view_transfers.x" ) };
const QString  SMS_EXEC_PREFMAINT    { QStringLiteral( "prefmaint.x" ) };
const QString  SMS_EXEC_S2STRANS     { QStringLiteral( "s2strans.x" ) };
const QString  SMS_EXEC_VDPPRTORD    { QStringLiteral( "vdpprtord.x" ) };
const QString  SMS_EXEC_VDPPRTORDFC  { QStringLiteral( "vdpprtordfc.x" ) };
const QString  SMS_EXEC_DSADJRPT     { QStringLiteral( "dsadjrpt.x" ) };
const QString  SMS_EXEC_EOD          { QStringLiteral( "eod.x" ) };
const QString  SMS_EXEC_KDOPEN       { QStringLiteral( "kdopen.x" ) };
const QString  SMS_EXEC_KDCLOSE      { QStringLiteral( "kdclose.x" ) };
const QString  SMS_EXEC_LANEASSG     { QStringLiteral( "lnadtasn.x" ) };
const QString  SMS_EXEC_PASSIGN      { QStringLiteral( "passign.x" ) };
const QString  SMS_EXEC_KPRMAN       { QStringLiteral( "kprman.x" ) };
const QString  SMS_EXEC_KPRHIST      { QStringLiteral( "kprhist.x" ) };

using AppType = GreenScreen::AppType;
using AppTypeMap = QMap<QString,AppType>;
const AppTypeMap  APP_TYPE_MAP  {
    { QStringLiteral( "NoApp" ),    GreenScreen::NoApp },
    { QStringLiteral( "Warranty" ), GreenScreen::Warranty },
    { QStringLiteral( "BackCounter" ), GreenScreen::BackCounter },
    { QStringLiteral( "CoreReturn" ), GreenScreen::CoreReturn },
    { QStringLiteral( "DamagedReturn" ), GreenScreen::DamagedReturn },
    { QStringLiteral( "UndamagedReturn" ), GreenScreen::UndamagedReturn },
    { QStringLiteral( "ForcedReturn" ), GreenScreen::ForcedReturn },
    { QStringLiteral( "VdpPickup" ), GreenScreen::VdpPickup },
    { QStringLiteral( "VdpOrder" ), GreenScreen::VdpOrder },
    { QStringLiteral( "ItemLookup" ), GreenScreen::ItemLookup },
    { QStringLiteral( "ItemLookupManager" ), GreenScreen::ItemLookupManager },
    { QStringLiteral( "ItemLookupSSC" ), GreenScreen::ItemLookupSSC },
    { QStringLiteral( "MndReport" ), GreenScreen::MndReport },
    { QStringLiteral( "UnbalancedPieceCounts" ), GreenScreen::UnbalancedPieceCounts },
    { QStringLiteral( "ViewPCIbyCSR" ), GreenScreen::ViewPCIbyCSR },
    { QStringLiteral( "CsrReport" ), GreenScreen::CsrReport },
    { QStringLiteral( "InHouseInvPrep" ), GreenScreen::InHouseInvPrep },
    { QStringLiteral( "RunPwdMaint" ), GreenScreen::RunPwdMaint },
    { QStringLiteral( "PhyInv" ), GreenScreen::PhyInv },
    { QStringLiteral( "SetupRemoveLoanerZoner" ), GreenScreen::SetupRemoveLoanerZoner },
    { QStringLiteral( "WarrantyLookup" ), GreenScreen::WarrantyLookup },
    { QStringLiteral( "ZonerMenu" ), GreenScreen::ZonerMenu },
    { QStringLiteral( "OilRecyclingStandard" ), GreenScreen::OilRecyclingStandard },
    { QStringLiteral( "OilRecyclingPR" ), GreenScreen::OilRecyclingPR },
    { QStringLiteral( "BatteryCharging" ), GreenScreen::BatteryCharging },
    { QStringLiteral( "ManagerMenu" ), GreenScreen::ManagerMenu },
    { QStringLiteral( "ChangeOrderFeatures" ), GreenScreen::ChangeOrderFeatures },
    { QStringLiteral( "Overstock" ), GreenScreen::Overstock },
    { QStringLiteral( "PettyCashLog" ), GreenScreen::PettyCashLog },
    { QStringLiteral( "RegisterAudit" ), GreenScreen::RegisterAudit },
    { QStringLiteral( "DepositPreparation" ), GreenScreen::DepositPreparation },
    { QStringLiteral( "DepositPickUp" ), GreenScreen::DepositPickUp },
    { QStringLiteral( "DepositHistReport" ), GreenScreen::DepositHistReport },
    { QStringLiteral( "CommRecon" ), GreenScreen::CommRecon },
    { QStringLiteral( "RegisterRecon" ), GreenScreen::RegisterRecon },
    { QStringLiteral( "RegisterSweep" ), GreenScreen::RegisterSweep },
    { QStringLiteral( "RegularReceipts" ), GreenScreen::RegularReceipts },
    { QStringLiteral( "StandaloneReceipts" ), GreenScreen::StandaloneReceipts },
    { QStringLiteral( "BMDeliveryRoutes" ), GreenScreen::BMDeliveryRoutes },
    { QStringLiteral( "BMDriverRoutes" ), GreenScreen::BMDriverRoutes },
    { QStringLiteral( "ReprintPedimentoRpts" ), GreenScreen::ReprintPedimentoRpts },
    { QStringLiteral( "BMPickingTours" ), GreenScreen::BMPickingTours },
    { QStringLiteral( "ViewPickingHistory" ), GreenScreen::ViewPickingHistory },
    { QStringLiteral( "PrintPickingLabels" ), GreenScreen::PrintPickingLabels },
    { QStringLiteral( "PrintTruckVIN" ), GreenScreen::PrintTruckVIN },
    { QStringLiteral( "ReprintRecentManifest" ), GreenScreen::ReprintRecentManifest },
    { QStringLiteral( "SlotQtyVarRpt" ), GreenScreen::SlotQtyVarRpt },
    { QStringLiteral( "PostTruckInvoice" ), GreenScreen::PostTruckInvoice },
    { QStringLiteral( "PrintOfflineReports" ), GreenScreen::PrintOfflineReports },
    { QStringLiteral( "ManagerOfficeEquipment" ), GreenScreen::ManagerOfficeEquipment },
    { QStringLiteral( "AccountReport" ), GreenScreen::AccountReport },
    { QStringLiteral( "ViewPrintDynamicSlots" ), GreenScreen::ViewPrintDynamicSlots },
    { QStringLiteral( "GenerateSlotsLabels" ), GreenScreen::GenerateSlotsLabels },
    { QStringLiteral( "ChkInTransfInbound" ), GreenScreen::ChkInTransfInbound },
    { QStringLiteral( "ViewDSDOrders" ), GreenScreen::ViewDSDOrders },
    { QStringLiteral( "DSDStatusHist" ), GreenScreen::DSDStatusHist },
    { QStringLiteral( "ViewHubTransfInbound" ), GreenScreen::ViewHubTransfInbound },
    { QStringLiteral( "ReceiveTransfInbound" ), GreenScreen::ReceiveTransfInbound },
    { QStringLiteral( "ReceiveByItem" ), GreenScreen::ReceiveByItem },
    { QStringLiteral( "ReceiveMiscItems" ), GreenScreen::ReceiveMiscItems },
    { QStringLiteral( "PrintProductPogs" ), GreenScreen::PrintProductPogs },
    { QStringLiteral( "PrintInstructionalPogs" ), GreenScreen::PrintInstructionalPogs },
    { QStringLiteral( "ReceiveByItem" ), GreenScreen::ReceiveByItem },
    { QStringLiteral( "ReviewHubStoreOrders" ), GreenScreen::ReviewHubStoreOrders },
    { QStringLiteral( "ViewStoreToStoreTransferInbound" ), GreenScreen::ViewStoreToStoreTransferInbound },
    { QStringLiteral( "ReplenishmentOrder" ), GreenScreen::ReplenishmentOrder },
    { QStringLiteral( "TruckInvoiceCheckIn" ), GreenScreen::TruckInvoiceCheckIn },
    { QStringLiteral( "ReceiveUCC128" ), GreenScreen::ReceiveUCC128 },
    { QStringLiteral( "ReceiveVDP" ), GreenScreen::ReceiveVDP },
    { QStringLiteral( "LocCodeLabels" ), GreenScreen::LocCodeLabels },  
    { QStringLiteral( "FloorLocDim" ), GreenScreen::FloorLocDim },    
    { QStringLiteral( "FloorLocDimAppr" ), GreenScreen::FloorLocDimAppr },
    { QStringLiteral( "UPOGLocCodeRpt" ), GreenScreen::UPOGLocCodeRpt },  
    { QStringLiteral( "RfLogoff" ), GreenScreen::RfLogoff },  //    
    { QStringLiteral( "AddToActualCount" ), GreenScreen::AddToActualCount },   
    { QStringLiteral( "EmptyPackages" ), GreenScreen::EmptyPackages },   
    { QStringLiteral( "SubToActualCount" ), GreenScreen::SubToActualCount },   
    { QStringLiteral( "ClearanceReport" ), GreenScreen::ClearanceReport },   
    { QStringLiteral( "GclearanceReport" ), GreenScreen::GclearanceReport }, /*fffff*/
    { QStringLiteral( "BuildInventoryFiles" ), GreenScreen::BuildInventoryFiles }, 
    { QStringLiteral( "ViewPreCountPogs" ), GreenScreen::ViewPreCountPogs },
    { QStringLiteral( "PrintPogTags" ), GreenScreen::PrintPogTags },  
    { QStringLiteral( "EnterMiscItems" ), GreenScreen::EnterMiscItems }, 
    { QStringLiteral( "EnterCountTeams" ), GreenScreen::EnterCountTeams }, 
    { QStringLiteral( "ItemReturnReport" ), GreenScreen::ItemReturnReport }, 
    { QStringLiteral( "BatteryUtilities" ), GreenScreen::BatteryUtilities }, 
    { QStringLiteral( "BatteryWorkSheet" ), GreenScreen::BatteryWorkSheet },
    { QStringLiteral( "PCIManifestReport" ), GreenScreen::PCIManifestReport },  
    { QStringLiteral( "PCIModification" ), GreenScreen::PCIModification },  
    { QStringLiteral( "PCIModificationReport" ), GreenScreen::PCIModificationReport }, 
    { QStringLiteral( "ReprintPCILabel" ), GreenScreen::ReprintPCILabel }, 
    { QStringLiteral( "StoreDamages" ), GreenScreen::StoreDamages }, 
    { QStringLiteral( "UndamagedVDPReturns" ), GreenScreen::UndamagedVDPReturns }, 
    { QStringLiteral( "ViewByPCIPart" ), GreenScreen::ViewByPCIPart },
    { QStringLiteral( "ViewPCISignByCSR" ), GreenScreen::ViewPCISignByCSR },  
    { QStringLiteral( "ViewPCISignByDateRange" ), GreenScreen::ViewPCISignByDateRange },
    { QStringLiteral( "WarrantyMaintenance" ), GreenScreen::WarrantyMaintenance },  
    { QStringLiteral( "CheckQOHReport" ), GreenScreen::CheckQOHReport }, 
    { QStringLiteral( "CreditCardReport" ), GreenScreen::CreditCardReport }, 
    { QStringLiteral( "CCTReport" ), GreenScreen::CCTReport }, 
    { QStringLiteral( "HPPriceBook" ), GreenScreen::HPPriceBook },
    { QStringLiteral( "MasterTrackingSheet" ), GreenScreen::MasterTrackingSheet }, 
    { QStringLiteral( "SystemActivityReport" ), GreenScreen::SystemActivityReport },  
    { QStringLiteral( "UnadjClkHistoryRpt" ), GreenScreen::UnadjClkHistoryRpt }, 
    { QStringLiteral( "WeeklyLaneRpt" ), GreenScreen::WeeklyLaneRpt }, 
    { QStringLiteral( "StatusReport" ), GreenScreen::StatusReport }, 
    { QStringLiteral( "InventoryControlRpts" ), GreenScreen::InventoryControlRpts },
    { QStringLiteral( "AudtRecSummaryRpt" ), GreenScreen::AudtRecSummaryRpt },
    { QStringLiteral( "SweepLogAudtRpt" ), GreenScreen::SweepLogAudtRpt },
    { QStringLiteral( "HelpDesk" ), GreenScreen::HelpDesk },
    { QStringLiteral( "DlyHoursRpt" ), GreenScreen::DlyHoursRpt },
    { QStringLiteral( "WklyHoursRpt" ), GreenScreen::WklyHoursRpt },
    { QStringLiteral( "Inv2InvAdjRpt" ), GreenScreen::Inv2InvAdjRpt },
    { QStringLiteral( "WkItemAdjusts" ), GreenScreen::WkItemAdjusts },
    { QStringLiteral( "DetailedItemAdjust" ), GreenScreen::DetailedItemAdjust },
    { QStringLiteral( "InvMgmtReport" ), GreenScreen::InvMgmtReport },
    { QStringLiteral( "PriceAccuracyRpt" ), GreenScreen::PriceAccuracyRpt },
    { QStringLiteral( "PrntLocationRpt" ), GreenScreen::PrntLocationRpt },
    { QStringLiteral( "DailyBackup" ), GreenScreen::DailyBackup },
    { QStringLiteral( "EmergencyContact" ), GreenScreen::EmergencyContact },
    { QStringLiteral( "HubDeliveryTimeEntry" ), GreenScreen::HubDeliveryTimeEntry },
    { QStringLiteral( "NameBadgeLabels" ), GreenScreen::NameBadgeLabels },
    { QStringLiteral( "StoreParameters" ), GreenScreen::StoreParameters },
    { QStringLiteral( "TaxExcemptInfo" ), GreenScreen::TaxExcemptInfo },
    { QStringLiteral( "ViewCurrentStoreVehicles" ), GreenScreen::ViewCurrentStoreVehicles },
    { QStringLiteral( "ViewPendingTransferVehicles" ), GreenScreen::ViewPendingTransferVehicles },
    { QStringLiteral( "ArrivalDateChangeReport" ), GreenScreen::ArrivalDateChangeReport },
    { QStringLiteral( "MgmtApprUnpaidOrdRpt" ), GreenScreen::MgmtApprUnpaidOrdRpt },
    { QStringLiteral( "PrintLogSinglePO" ), GreenScreen::PrintLogSinglePO },
    { QStringLiteral( "PrintLogDateRange" ), GreenScreen::PrintLogDateRange },
    { QStringLiteral( "ReceiveVDPParts" ), GreenScreen::ReceiveVDPParts },
    { QStringLiteral( "UnapprovedUnpaidVDPOrders" ), GreenScreen::UnapprovedUnpaidVDPOrders },
    { QStringLiteral( "UnrecvVDPPO" ), GreenScreen::UnrecvVDPPO },
    { QStringLiteral( "ViewVDPPO" ), GreenScreen::ViewVDPPO },
    { QStringLiteral( "VDPPOCancellations" ), GreenScreen::VDPPOCancellations },
    { QStringLiteral( "PrintShippingOrderDoc" ), GreenScreen::PrintShippingOrderDoc },
    { QStringLiteral( "Rutilities" ), GreenScreen::Rutilities },
    { QStringLiteral( "CycleCountMaint" ), GreenScreen::CycleCountMaint },
    { QStringLiteral( "KplnUtilities" ), GreenScreen::KplnUtilities },
    { QStringLiteral( "CatalogStoreWT" ), GreenScreen::CatalogStoreWT },   
    { QStringLiteral( "ItemNotOnPog" ), GreenScreen::ItemNotOnPog },   
    { QStringLiteral( "PriceChangesMenu" ), GreenScreen::PriceChangesMenu },   
    { QStringLiteral( "PrntLabels" ), GreenScreen::PrntLabels },   
    { QStringLiteral( "PrntBulkDSLabels" ), GreenScreen::PrntBulkDSLabels },   
    { QStringLiteral( "SkuBarcodePrintout" ), GreenScreen::SkuBarcodePrintout },   
    { QStringLiteral( "VdpOrderShipment" ), GreenScreen::VdpOrderShipment },
    { QStringLiteral( "OvrAndBuyerRecalls" ), GreenScreen::OvrAndBuyerRecalls },
    { QStringLiteral( "ExpeditedOrders" ), GreenScreen::ExpeditedOrders },
    { QStringLiteral( "ViewStoreToStoreTransferOutbound" ), GreenScreen::ViewStoreToStoreTransferOutbound },
    { QStringLiteral( "UndamagedReturns" ), GreenScreen::UndamagedReturns },
    { QStringLiteral( "HourlySalesReport" ), GreenScreen::HourlySalesReport },
    { QStringLiteral( "DLVTasks" ), GreenScreen::DLVTasks },
    { QStringLiteral( "Validation" ), GreenScreen::Validation },
    { QStringLiteral( "Maintenance" ), GreenScreen::Maintenance },
    { QStringLiteral( "EpoOrder" ), GreenScreen::EpoOrder },
    { QStringLiteral( "CommReturn" ), GreenScreen::CommReturn },
    { QStringLiteral( "StoreSummary" ), GreenScreen::StoreSummary },
    { QStringLiteral( "CommPayment" ), GreenScreen::CommPayment },
    { QStringLiteral( "ScanSignatures" ), GreenScreen::ScanSignatures },
    { QStringLiteral( "ClockInOut" ), GreenScreen::ClockInOut },
    { QStringLiteral( "Claims" ), GreenScreen::Claims },
    { QStringLiteral( "CycleCountHistory" ), GreenScreen::CycleCountHistory },
    { QStringLiteral( "InvoiceReprint" ), GreenScreen::InvoiceReprint },
    { QStringLiteral( "CommWarranty" ), GreenScreen::CommWarranty },
    { QStringLiteral( "CommercialStocking" ), GreenScreen::CommercialStocking },
    { QStringLiteral( "CommercialPhoneStickers" ), GreenScreen::CommercialPhoneStickers },
    { QStringLiteral( "CommercialInvoiceSignature" ), GreenScreen::CommercialInvoiceSignature },
    { QStringLiteral( "PrntOutsCoreDefRpt" ), GreenScreen::PrntOutsCoreDefRpt },
    { QStringLiteral( "CommercialOverrideReport" ), GreenScreen::CommercialOverrideReport },
    { QStringLiteral( "CommercialConsignment" ), GreenScreen::CommercialConsignment },
    { QStringLiteral( "CommCustInv" ), GreenScreen::CommCustInv },
    { QStringLiteral( "OutstandingBalances"), GreenScreen::OutstandingBalances},
    { QStringLiteral( "WDPurchaseOrder" ), GreenScreen::WDPurchaseOrder },
    { QStringLiteral( "CommercialReports" ), GreenScreen::CommercialReports },
    { QStringLiteral( "SmsRf" ), GreenScreen::SmsRf },
    { QStringLiteral( "WarrantyReturns" ), GreenScreen::WarrantyLookup },
    { QStringLiteral( "TransactionReturns" ), GreenScreen::TransactionReturns },
    { QStringLiteral( "PickerPerformanceRpts" ), GreenScreen::PickerPerformanceRpts },
    { QStringLiteral( "DriverPerformanceRpts" ), GreenScreen::DriverPerformanceRpts },
    { QStringLiteral( "SatelliteStorePerformanceRpts" ), GreenScreen::SatelliteStorePerformanceRpts },
    { QStringLiteral( "HubStorePerformanceRpts" ), GreenScreen::HubStorePerformanceRpts },
    { QStringLiteral( "QCRpts" ), GreenScreen::QCRpts },
    { QStringLiteral( "SkipItemHist" ), GreenScreen::SkipItemHist },
    { QStringLiteral( "OrdPrtLocCodeLbls" ), GreenScreen::OrdPrtLocCodeLbls },
    { QStringLiteral( "FloorLocAssign" ), GreenScreen::FloorLocAssign },
    { QStringLiteral( "FloorLocAppr" ), GreenScreen::FloorLocAppr },
    { QStringLiteral( "UnaPogLocCodeRpt" ), GreenScreen::UnaPogLocCodeRpt },
    { QStringLiteral( "DlyQCRpt" ), GreenScreen::DlyQCRpt },
    { QStringLiteral( "WklyQCRcvRpts" ), GreenScreen::WklyQCRcvRpts },
    { QStringLiteral( "DSDAgingRpt" ), GreenScreen::DSDAgingRpt },
    { QStringLiteral( "TranHistRpt" ), GreenScreen::TranHistRpt },
    { QStringLiteral( "FileMaintenance" ), GreenScreen::FileMaintenance },
    { QStringLiteral( "SatStorePerfRpts" ), GreenScreen::SatStorePerfRpts },
    { QStringLiteral( "SatStorePerfRpts" ), GreenScreen::SatStorePerfRpts },
    { QStringLiteral( "S2STrans" ), GreenScreen::S2STrans },
    { QStringLiteral( "VdpPrtOrd" ), GreenScreen::VdpPrtOrd },
    { QStringLiteral( "VdpPrtOrdFc" ), GreenScreen::VdpPrtOrdFc },
    { QStringLiteral( "DsReports" ), GreenScreen::DsReports },
    { QStringLiteral( "EndOfDay" ), GreenScreen::EndOfDay },
    { QStringLiteral( "OpeningTaskList" ), GreenScreen::OpeningTaskList },
    { QStringLiteral( "ClosingTaskList" ), GreenScreen::ClosingTaskList },
    { QStringLiteral( "LaneAssign" ), GreenScreen::LaneAssign },
    { QStringLiteral( "PriorityAssignment" ), GreenScreen::PriorityAssignment },
    { QStringLiteral( "ManualEntryPayrollHours" ), GreenScreen::ManualEntryPayrollHours },
    { QStringLiteral( "PayrollHistoryRpt" ), GreenScreen::PayrollHistoryRpt },
    { QStringLiteral( "ReprintEodPayrollRpt" ), GreenScreen::ReprintEodPayrollRpt },
    { QStringLiteral( "ReprintEowPayrollRpt" ), GreenScreen::ReprintEowPayrollRpt }
};
}

//#define  USE_LOCAL_ECHO

// PRIVATE DATA
class  GreenScreenPrivate
{
public:
    GreenScreenPrivate() = default;
   ~GreenScreenPrivate() = default;

public:
    Terminal*        terminal    { nullptr };
    ZnetCashDrawer*  cashDrawer  { nullptr };
    ZnetPrinter*     printer     { nullptr };
    ZnetScanner*     scanner     { nullptr };
    QTcpSocket       commDevice;
    AppType          appType;
    QString          appArgs;
    QString          errorStr;
    QString          commHost    { DEFAULT_COMM_HOST };
    int              commPort    { DEFAULT_COMM_PORT };
    QString          execCmd;
    QString          execPath    { DEFAULT_EXEC_PATH };
    QString          authKey;
    QString          envStr;
    QString          genCmd;
};


//============================================================================
//  P U B L I C   I N T E R F A C E
//============================================================================

//----------------------------------------------------------------------------

GreenScreen::GreenScreen( AppType appType, QWidget* parent )
    : QDialog( parent, Qt::Dialog | Qt::FramelessWindowHint )
{
    p.reset( new GreenScreenPrivate );

    QString  regHost = QString::fromLocal8Bit( qgetenv( ENV_CPOS_HOST ));
    if (regHost.isEmpty())
    {
        regHost = QStringLiteral( "localhost" );
    }

    p->appType = appType;

    ZnetCashDrawer*  cashdrawer = new ZnetCashDrawer( this );

    ZnetPrinter*  printer = new ZnetPrinter( this );
    printer->setHostName( regHost );

    ZnetScanner* scanner = new ZnetScanner( this );
    scanner->setHostName( regHost );
    scanner->setScanOnConnect();

    Terminal* terminal = new Terminal( *printer );

    QVBoxLayout*  layout = new QVBoxLayout;
    layout->addWidget( terminal, 1 );
    setLayout( layout );

    p->cashDrawer = cashdrawer;
    p->printer = printer;
    p->scanner = scanner;
    p->terminal = terminal;

    connect( terminal, &Terminal::sessionEnded,
             this,     &GreenScreen::sessionDoneCB );
#ifdef  USE_LOCAL_ECHO
    connect( terminal, &Terminal::sendData,
             terminal, &Terminal::receiveData );
#else
    connect( terminal, &Terminal::sendData,
             this,     &GreenScreen::commWriteCB );
    connect( &p->commDevice, &QTcpSocket::readyRead,
             this,           &GreenScreen::commReadCB );
    connect( &p->commDevice, &QTcpSocket::disconnected,
             p->terminal,    &Terminal::cleanup );
#endif
    connect( printer, &ZnetPrinter::printerResponse,
             this,    &GreenScreen::commWriteCB );
    connect( cashdrawer, &ZnetCashDrawer::cashDrawerAvailableDone,
             this,       &GreenScreen::cashDrawerAvailCB );
    connect( scanner, &ZnetScanner::scannerDataRead,
             this,    &GreenScreen::scannerReadCB );
}

//----------------------------------------------------------------------------

GreenScreen::~GreenScreen()
{
    // Nothing to do
}

//----------------------------------------------------------------------------

GreenScreen::AppType  GreenScreen::appType() const
{
    return  p->appType;
}

//----------------------------------------------------------------------------

QString  GreenScreen::appName() const
{
    return  APP_TYPE_MAP.key( appType(), QString() );
}

//----------------------------------------------------------------------------

QString  GreenScreen::appArgs() const
{
    return  p->appArgs;
}

//----------------------------------------------------------------------------

QString  GreenScreen::errorString() const
{
    return  p->errorStr;
}

//----------------------------------------------------------------------------
/** @fn     void  GreenScreen::configure( const QString& args )
 *  @param[in]  args       greenscreen args
 *
 *  Configures the request for zn_termd server to execute a greenscreen executable with the
 *  expected case, if a new screen is expected to be added, all the handling for
 *  the parameters and a case with the executable name and event name should be added.
 * \code
 * Modification History
 * MM/DD/YYYY   NAME        DESCRIPTION
 * 04/12/2024   lmorales    add eod execution handling case
 */
bool  GreenScreen::configure( const QString& args )
{
    ZnetCommon*  znc = ZnetCommon::znetCommon();
    if (!znc)
    {
        return  false;
    }

    p->commHost = znc->storeServer();
    p->commPort = znc->termPort();

    p->cashDrawer->requestCashDrawerAvailable();

    switch (appType())
    {
      case  Generic:
        p->execCmd = p->genCmd;
        break;

      case  Warranty:
        p->execCmd = SMS_EXEC_KENTWAR;
        break;

      case  BackCounter:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = args;
        break;

      case  CoreReturn:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QString( "-c %1" ).arg( args );
        break;

      case  DamagedReturn:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QStringLiteral( "-d" );
        break;

      case  UndamagedReturn:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QStringLiteral( "-u" );
        break;

      case  ForcedReturn:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QString( "-f %1" ).arg( args );
        break;

      case  TransactionReturns:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QString( "-t %1" ).arg( args );
        break;

      case  VdpPickup:
        p->execCmd = SMS_EXEC_KEPASRCV;
        break;
      
      case  LocCodeLabels:
        p->execCmd = SMS_EXEC_LCORDPRN;
        break;
      
      case  FloorLocDimAppr:
        p->execCmd = SMS_EXEC_KFLRVER;
        break;
            
      case  UPOGLocCodeRpt:
        p->execCmd = SMS_EXEC_LOCPOGRPT;
        break;

      case  VdpOrder:
        p->execCmd = SMS_EXEC_KEPAORDE;
        if (args.isEmpty())
        {
            p->appArgs = QStringLiteral( "-z" );
        }
        else
        {
            p->appArgs = QString( "-z %1" ).arg( args );
        }
        break;

      case  ItemLookup:
        p->execCmd = SMS_EXEC_KITMOVR;
        break;

      case  ItemLookupManager:
        p->execCmd = SMS_EXEC_PLRLOOKUP;
        p->appArgs = QStringLiteral( "-a" );
        break;
      
      case  PrintProductPogs:
        p->execCmd = SMS_EXEC_KPRTPOG;
        p->appArgs = QStringLiteral( " 1" );
        break;
      
      case  PrintInstructionalPogs:
        p->execCmd = SMS_EXEC_KPRTPOG;
        p->appArgs = QStringLiteral( " 2" );
        break;

      case  ItemLookupSSC:
        p->execCmd = SMS_EXEC_PLRLOOKUP;
        p->appArgs = QStringLiteral( "-s" );
        break;

      case  MndReport:
        p->execCmd = SMS_EXEC_MNDRREPORT;
        p->appArgs = QStringLiteral( "Y" );
        break;

      case  UnbalancedPieceCounts:
        p->execCmd = SMS_EXEC_AUCNTRPT;
        break;

      case  ViewPCIbyCSR:
        p->execCmd = SMS_EXEC_PCISIGRPT;
        p->appArgs = QStringLiteral( "C" );
        break;
      
      case  CsrReport:
        p->execCmd = SMS_EXEC_KEMPLO;
        p->appArgs = QStringLiteral( " %1" ).arg( args );
        break;

      case  InHouseInvPrep:
        p->execCmd = SMS_EXEC_INVPREMENU;
        break;

      case  RunPwdMaint:
        p->execCmd = SMS_EXEC_KPASSWD;
        break;

      case  PhyInv:
        p->execCmd = SMS_EXEC_INVMENU;
        break;

      case  SetupRemoveLoanerZoner:
        p->execCmd = SMS_EXEC_KEMPLO;
        p->appArgs = QStringLiteral( " LOANERZONER" );
        break;

      case  WarrantyLookup:
        p->execCmd = SMS_EXEC_PCR;
        p->appArgs = QStringLiteral( "-w" );
        if (!args.isEmpty())
        {
            p->appArgs += QLatin1String( " " ) + args;
        }
        break;

      case  ZonerMenu:
        p->execCmd = SMS_EXEC_WITTF16MENU;
        p->appArgs = args;
        break;

      case  OilRecyclingStandard:
        p->execCmd = SMS_EXEC_KUOILMNU;
        break;

      case  OilRecyclingPR:
        p->execCmd = SMS_EXEC_KUOILREFPAY;
        break;

      case  BatteryCharging:
        p->execCmd = SMS_EXEC_KBAT;
        break;

      case  ManagerMenu:
        p->execCmd = SMS_EXEC_KLOGIN;
        break;
      
      case  ChangeOrderFeatures:
        p->execCmd = SMS_EXEC_KCHORD;
        break;

      case  Overstock:
        p->execCmd = SMS_EXEC_OVERSTOCK;
        break;

      case  PettyCashLog:
        p->execCmd = SMS_EXEC_KPCASH;
        break;

      case  RegisterAudit:
        p->execCmd = SMS_EXEC_LANEACCT;
        p->appArgs = QStringLiteral( "A%1" ).arg( args );
        break;

      case  DepositPreparation:
        p->execCmd = SMS_EXEC_RFDEPOSIT;
        p->appArgs = QStringLiteral( "D%1" ).arg( args );
        break;

      case  DepositPickUp:
        p->execCmd = SMS_EXEC_RFDEPOSIT;
        p->appArgs = QStringLiteral( "P%1" ).arg( args );
        break;

      case  DepositHistReport:
        p->execCmd = SMS_EXEC_DEPHIST;
        break;

      case  CommRecon:
        p->execCmd = SMS_EXEC_KGREGIS;
        p->appArgs = QStringLiteral( "C%1" ).arg( args );
        break;

      case  RegisterRecon:
        p->execCmd = SMS_EXEC_KGREGIS;
        p->appArgs = QStringLiteral( "D%1" ).arg( args );
        break;

      case  RegisterSweep:
        p->execCmd = SMS_EXEC_LANEACCT;
        p->appArgs = QStringLiteral( "S%1" ).arg( args );
        break;

      case  RegularReceipts:
      case  StandaloneReceipts:
        p->execCmd = SMS_EXEC_KELRCPT;
        break;

      case  BMDeliveryRoutes:
        p->execCmd = SMS_EXEC_IMASSIGNRT;
        break;

      case  BMDriverRoutes:
        p->execCmd = SMS_EXEC_IMMONDRV;
        break;

      case ReprintPedimentoRpts:
        p->execCmd = SMS_EXEC_PEDIMENTO;
        break;

      case  BMPickingTours:
        p->execCmd = SMS_EXEC_IMORDMAG;
        break;

      case  ViewPickingHistory:
        p->execCmd = SMS_EXEC_IMVIEWORDRS;
        p->appArgs = QStringLiteral( "outbound" );
        break;

      case  PrintPickingLabels:
        p->execCmd = SMS_EXEC_IMPRNTLBLS;
        break;

      case  PrintTruckVIN:
        p->execCmd = SMS_EXEC_IMPRTVIN;
        break;

      case  ReprintRecentManifest:
        p->execCmd = SMS_EXEC_IMREPMANIF;
        break;

      case  ViewPrintDynamicSlots:
        p->appArgs = QStringLiteral( "b" );
        p->execCmd = SMS_EXEC_RSLOTVIEW;
        break;

      case  GenerateSlotsLabels:
        p->appArgs = QStringLiteral( "c" );
        p->execCmd = SMS_EXEC_RSLOTVIEW;
        break;

      case  ChkInTransfInbound:
        p->execCmd = SMS_EXEC_IMSHIPACK;
        p->appArgs = args;
        break;

      case  ViewDSDOrders:
        p->execCmd = SMS_EXEC_KPO;
        break;

      case  DSDStatusHist:
        p->execCmd = SMS_EXEC_DSDRPT;
        break;

      case  ViewHubTransfInbound:
        p->execCmd = SMS_EXEC_IMVIEWORDRS;
        p->appArgs = QStringLiteral( "inbound" );
        break;

      case  ReceiveTransfInbound:
        p->execCmd = SMS_EXEC_IMRECVING;
        break;

      case  ReceiveByItem:
        p->execCmd = SMS_EXEC_RFDSDRC;
        break;

      case  ReceiveMiscItems:
        p->execCmd = SMS_EXEC_KRMISC;
        break;

      case  ReviewHubStoreOrders:
        p->execCmd = SMS_EXEC_EPVIEW;
        break;

      case  ViewStoreToStoreTransferInbound:
        p->execCmd = SMS_EXEC_IMVIEWORDRS;
        p->appArgs = QStringLiteral( "S2S IN" );
        break;

      case  S2STrans:
        p->execCmd = SMS_EXEC_S2STRANS;
        break;
      
      case VdpPrtOrd:
        p->execCmd = SMS_EXEC_VDPPRTORD;
        break;

      case VdpPrtOrdFc:
        p->execCmd = SMS_EXEC_VDPPRTORDFC;
        break;
      
      case DsReports:
        p->execCmd = SMS_EXEC_DSADJRPT;
        break;

      case  ReplenishmentOrder:
        p->execCmd = SMS_EXEC_PLRMENU;
        break;

      case  TruckInvoiceCheckIn:
        p->execCmd = SMS_EXEC_IMDCACK;
        break;

      case  TruckInvoiceReceiving:
        p->execCmd = SMS_EXEC_IMDCRCV;
        break;

      case  ReceiveUCC128:
        p->execCmd = SMS_EXEC_RFDSD;
        break;

      case  ReceiveVDP:
        p->execCmd = SMS_EXEC_IMRCVDP;
        break;

      case  RfLogoff:
        p->execCmd = SMS_EXEC_RFLOGOFF;
        break;

      case  VdpOrderShipment:
        p->execCmd = SMS_EXEC_VDPSHIP;
        break;

      case  OvrAndBuyerRecalls:
        p->execCmd = SMS_EXEC_RCMENU;
        break;

      case  ExpeditedOrders:
        p->execCmd = SMS_EXEC_EPVIEW;
        p->appArgs = QStringLiteral( " E" );
        break;

      case  ViewStoreToStoreTransferOutbound:
        p->execCmd = SMS_EXEC_IMVIEWORDRS;
        p->appArgs = QStringLiteral( "S2S OUT" );
        break;

      case  UndamagedReturns:
        p->execCmd = SMS_EXEC_RCSTORE;
        break;

      case  HourlySalesReport:
        p->execCmd = SMS_EXEC_KAUTOHOUR;
        p->appArgs = QStringLiteral( "S" ).arg( args );
        break;

      case  AddToActualCount:
      case  EmptyPackages:
      case  SubToActualCount:
        p->execCmd = SMS_EXEC_KIADJUST;
        p->appArgs = QStringLiteral( "D MAINTITM" );
        break;
      
      /* important, run first kclrpt.x to get an updated report*/
      case  ClearanceReport:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = QStringLiteral( " clr_items.rpt" );
        break;

      case  GclearanceReport:
        p->execCmd = SMS_EXEC_KCLRRPT;
        break;

      case  BuildInventoryFiles:
        p->execCmd = SMS_EXEC_INVBLD;
        break;

      case  ViewPreCountPogs:
        p->execCmd = SMS_EXEC_INVVIEWPRE;
        break;

      case  PrintPogTags:
        p->execCmd = SMS_EXEC_INVTAG;
        break;

      case  EnterMiscItems:
        p->execCmd = SMS_EXEC_INVMISC;
        break;

      case  EnterCountTeams:
        p->execCmd = SMS_EXEC_INVTEAM;
        break;

      case  ItemReturnReport:
        p->execCmd = SMS_EXEC_KPRETURNS;
        break;

      case  BatteryUtilities:
        p->execCmd = SMS_EXEC_KBATLOG;
        break;

      case  BatteryWorkSheet:
        p->execCmd = SMS_EXEC_KBATWS;
        break;

      case  PCIManifestReport:
        p->execCmd = SMS_EXEC_KPMANRPT;
        break;

      case  PCIModification:
        p->execCmd = SMS_EXEC_KPCIMOD;
        break;

      case  PCIModificationReport:
        p->execCmd = SMS_EXEC_KPMODRPT;
        break;

      case  ReprintPCILabel:
        p->execCmd = SMS_EXEC_KPCIREP;
        break;

      case  StoreDamages:
        p->execCmd = SMS_EXEC_KSTDAMAGES;
        break;

      case  UndamagedVDPReturns:
        p->execCmd = SMS_EXEC_KSTDAMAGES;
        break;

      case  ViewByPCIPart:
        p->execCmd = SMS_EXEC_KPCILKUP;
        break;

      case  ViewPCISignByCSR:
        p->execCmd = SMS_EXEC_PCISIGRPT;
        p->appArgs = QStringLiteral( "C" );
        break;

      case  ViewPCISignByDateRange:
        p->execCmd = SMS_EXEC_PCISIGRPT;
        p->appArgs = QStringLiteral( "D" );
        break;

      case  WarrantyMaintenance:
        p->execCmd = SMS_EXEC_AWARLKUP;
        break;

      case  CheckQOHReport:
        p->execCmd = SMS_EXEC_KQTYRPT;
        p->appArgs = QStringLiteral( " -d %1" ).arg(args);
        break;

      case  CreditCardReport:
        p->execCmd = SMS_EXEC_KCCRPT;
        break;

      case  CCTReport:
        p->execCmd = SMS_EXEC_KCCRPT;
        p->appArgs = QStringLiteral( " C" );
        break;

      case  HPPriceBook:
        p->execCmd = SMS_EXEC_KPRIBOOK;
        break;

      case  MasterTrackingSheet:
        p->execCmd = SMS_EXEC_MASTERTRKRPT;
        p->appArgs = QStringLiteral( " MGR" );
        break;

      case  SystemActivityReport:
        p->execCmd = SMS_EXEC_KMSYSRPT;
        break;

      case  UnadjClkHistoryRpt:
        p->execCmd = SMS_EXEC_PUNCHRPT;
        break;

      case  StatusReport:
        p->execCmd = SMS_EXEC_KDSTATUS;
        break;

      case  InventoryControlRpts:
        p->execCmd = SMS_EXEC_KRITMPLN;
        break;

      case  AudtRecSummaryRpt:
        p->execCmd = SMS_EXEC_ADTRPTS;
        break;

      case  SweepLogAudtRpt:
        p->execCmd = SMS_EXEC_LANEHISTRPT;
        break;

      case  WeeklyLaneRpt:
        p->execCmd = SMS_EXEC_WKLYLNRPT;
        break;

      case  HelpDesk:
        p->execCmd = SMS_EXEC_HLPDSK;
        break;

      case  DlyHoursRpt:
        p->execCmd = SMS_EXEC_SMSCLKEOD;
        p->appArgs = QStringLiteral( " -p" );
        break;

      case  WklyHoursRpt:
        p->execCmd = SMS_EXEC_SMSCLKEOW;
        p->appArgs = QStringLiteral( " -p" );
        break;

      case  Inv2InvAdjRpt:
        p->execCmd = SMS_EXEC_INV2INV;
        break;

      case  WkItemAdjusts:
        p->execCmd = SMS_EXEC_RPADJRPT;
        break;

      case  DetailedItemAdjust:
        p->execCmd = SMS_EXEC_ADJDETAIL;
        break;

      case  InvMgmtReport:
        p->execCmd = SMS_EXEC_NIMBLDRPT;
        p->appArgs = QStringLiteral( " L" );
        break;

      case  PostTruckInvoice:
        p->execCmd = SMS_EXEC_KSPRINT;
        p->appArgs = QStringLiteral( " B" );
        break;

      case  PrintOfflineReports:
        p->execCmd = SMS_EXEC_KSA_MENU;
        break;
      
      case  ManagerOfficeEquipment:
        p->execCmd = SMS_EXEC_DTMENU;
        break;

      case  AccountReport:
        p->execCmd = SMS_EXEC_KDWACCT;
        break;

      case  PriceAccuracyRpt:
        p->execCmd = SMS_EXEC_INMSELDATE;
        break;

      case  PrntLocationRpt:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = QStringLiteral( " multiple_locations.rpt" );
        break;

      case  DailyBackup:
        p->execCmd = SMS_EXEC_KDBACKUP;
        break;

      case  EmergencyContact:
        p->execCmd = SMS_EXEC_KEMPINFO;
        break;

      case  HubDeliveryTimeEntry:
        p->execCmd = SMS_EXEC_HUBDLVRSCRN;
        break;

      case  NameBadgeLabels:
        p->execCmd = SMS_EXEC_KPRTBADGE;
        break;

      case  StoreParameters:
        p->execCmd = SMS_EXEC_KLSTORE;
        break;

      case  TaxExcemptInfo:
        p->execCmd = SMS_EXEC_KLEXEMPT;
        break;

      case  ViewCurrentStoreVehicles:
        p->execCmd = SMS_EXEC_FLEETVIEW;
        break;

      case  ViewPendingTransferVehicles:
        p->execCmd = SMS_EXEC_FLEETVIEW;
        break;

      case  ArrivalDateChangeReport:
        p->execCmd = SMS_EXEC_RPT5PM;
        break;

      case  MgmtApprUnpaidOrdRpt:
        p->execCmd = SMS_EXEC_VDPREPT;
        p->appArgs = QStringLiteral( " klogin UNPAID" );
        break;

      case  PrintLogSinglePO:
        p->execCmd = SMS_EXEC_KEPASPRINT;
        p->appArgs = QStringLiteral( " %1 VIEW" ).arg( args );
        break;

      case  PrintLogDateRange:
        p->execCmd = SMS_EXEC_KEPASPRINT;
        p->appArgs = QStringLiteral( " DATE %1 VIEW" ).arg( args );
        break;

      case  ReceiveVDPParts:
        p->execCmd = SMS_EXEC_KEPASDLVR;
        break;

      case  UnapprovedUnpaidVDPOrders:
        p->execCmd = SMS_EXEC_VDPREPT;
        p->appArgs = QStringLiteral( " klogin CUST_NOT_PAID" );
        break;

      case  UnrecvVDPPO:
        p->execCmd = SMS_EXEC_PRTOUTSPO;
        break;

      case  ViewVDPPO:
        p->execCmd = SMS_EXEC_KEPASLOG;
        break;

      case  VDPPOCancellations:
        p->execCmd = SMS_EXEC_KEXPCAN;
        break;

      case  PrintShippingOrderDoc:
        p->execCmd = SMS_EXEC_PRTVDPORD;
        break;

      case  Rutilities:
        p->execCmd = SMS_EXEC_KUTILITY;
        break;

      case  CycleCountHistory:
        p->execCmd = SMS_EXEC_KDCYCCNT;
        break;

      case  CycleCountMaint:
        p->execCmd = SMS_EXEC_KDCYCLE;
        break;
      
      case KplnUtilities:
        p->execCmd = SMS_EXEC_KPLNUTIL;
        break;

      case  CatalogStoreWT:
        p->execCmd = SMS_EXEC_KCATLG;
        break;

      case  ItemNotOnPog:
        p->execCmd = SMS_EXEC_NOPOGRPT;
        break;

      case  PriceChangesMenu:
        p->execCmd = SMS_EXEC_PRCHGMENU;
        break;

      case  PrntLabels:
        p->execCmd = SMS_EXEC_LBLMENU;
        break;

      case  PrntBulkDSLabels:
        p->execCmd = SMS_EXEC_RSLOTVIEW;
        p->appArgs = QStringLiteral( "d" );
        break;

      case  SkuBarcodePrintout:
        p->execCmd = SMS_EXEC_KSKUBARCODLS;
        break;

      case  SlotQtyVarRpt:
        p->execCmd = SMS_EXEC_SLOTQOHVAR;
        break;

      case  DLVTasks:
        p->execCmd = SMS_EXEC_DLVTASKS;
        break;

      case  Validation:
        p->execCmd = SMS_EXEC_ORDCMPLTN;
        break;

      case  Maintenance:
        p->execCmd = SMS_EXEC_ORDRMGMT;
        p->appArgs = QStringLiteral( "-m" );
        break;

      case  StoreSummary:
        p->execCmd = SMS_EXEC_ORDRMGMT;
        p->appArgs = QStringLiteral( "-s" );
        break;

      case  CommReturn:
        p->execCmd = SMS_EXEC_OMRETURNS;
        p->appArgs = args;
        break;

      case  CommPayment:
        p->execCmd = SMS_EXEC_KDRVRTRK;
        p->appArgs = args;
        break;

      case  EpoOrder:
        p->execCmd = SMS_EXEC_KEPO;
        p->appArgs = QString( "-m 1 %1" ).arg( args );
        break;

      case  ScanSignatures:
        p->execCmd = SMS_EXEC_KSCANINVSIG;
        p->appArgs = args;
        break;

      case  ClockInOut:
        p->execCmd = SMS_EXEC_AZCLKUTIL;
        p->appArgs = args;
        break;

      case  Claims:
        p->execCmd = SMS_EXEC_CLAIMS;
        p->appArgs = args;
        break;

      case  InvoiceReprint:
        p->execCmd = SMS_EXEC_KREPRINT;
        p->appArgs = args;
        break;

      case  CommWarranty:
        p->execCmd = SMS_EXEC_AWARLKUP;
        p->appArgs = args;
        break;

      case  CommercialPhoneStickers:
        p->execCmd = SMS_EXEC_KCASTIK;
        break;

      case  PrntOutsCoreDefRpt:
        p->execCmd = SMS_EXEC_KDEFCRPT;
        break;

      case  CommercialInvoiceSignature:
        p->execCmd = SMS_EXEC_SIGNRPT;
        break;

      case  CommercialOverrideReport:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = QString( "%1" ).arg( args );
        break;

      case  CommCustInv:
        p->execCmd = SMS_EXEC_CUSTINV;
        p->appArgs = args;
        break;

      case  OutstandingBalances:
        p->execCmd = SMS_EXEC_KAZODLY;
        p->appArgs = QString( "-O%1" ).arg( args ); // No space ???
        break;

      case  WDPurchaseOrder:
        p->execCmd = SMS_EXEC_KEPO;
        p->appArgs = args;
        break;

      case  CommercialReports:
        p->execCmd = SMS_EXEC_EXPCCRPT;
        p->appArgs = args;
        break;

      case  SmsRf:
        p->execCmd = SMS_EXEC_KRFMENU;
        p->appArgs = args;
        break;

      case  PickerPerformanceRpts:
        p->execCmd = SMS_EXEC_IMPICKERRPT;
        p->appArgs = args;
        break;

      case  DriverPerformanceRpts:
        p->execCmd = SMS_EXEC_IMDRPT;
        p->appArgs = args;
        break;

      case  SatelliteStorePerformanceRpts:
        p->execCmd = SMS_EXEC_IMSATRPT;
        p->appArgs = args;
        break;

      case  HubStorePerformanceRpts:
        p->execCmd = SMS_EXEC_IMHUBRPT;
        p->appArgs = args;
        break;

      case  QCRpts:
        p->execCmd = SMS_EXEC_IM_QC_RPT;
        p->appArgs = args;
        break;

      case  SkipItemHist:
        p->execCmd = SMS_EXEC_IMSKIPSKU;
        p->appArgs = args;
        break;

      case  OrdPrtLocCodeLbls:
        p->execCmd = SMS_EXEC_LCORDPRN;
        p->appArgs = args;
        break;

      case  FloorLocAssign:
        p->execCmd = SMS_EXEC_KPLNEDT;
        p->appArgs = args;
        break;

      case  FloorLocDim:
        p->execCmd = SMS_EXEC_KFLRIDS;
        p->appArgs = args;
        break;

      case  FloorLocAppr:
        p->execCmd = SMS_EXEC_KFLRVER;
        p->appArgs = args;
        break;

      case  UnaPogLocCodeRpt:
        p->execCmd = SMS_EXEC_LOCPOGRPT;
        p->appArgs = args;
        break;

      case  DlyQCRpt:
        p->execCmd = SMS_EXEC_IMQCRPT;
        p->appArgs = args;
        break;

      case  WklyQCRcvRpts:
        p->execCmd = SMS_EXEC_IMRCVRPT;
        p->appArgs = args;
        break;

      case  DSDAgingRpt:
        p->execCmd = SMS_EXEC_IMDSDRPT;
        p->appArgs = args;
        break;

      case  TranHistRpt:
        p->execCmd = SMS_EXEC_IMVIEWTRANSF;
        p->appArgs = args;
        break;

      case  FileMaintenance:
        p->execCmd = SMS_EXEC_PREFMAINT;
        break;

      case  SatStorePerfRpts:
        p->execCmd = SMS_EXEC_IMSATRPT;
        p->appArgs = args;
        break;

      case  RptUtil:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = args;
        break;
      
      case  EndOfDay:
        p->execCmd = SMS_EXEC_EOD;
        p->appArgs = args;
        break;

      case  OpeningTaskList:
        p->execCmd = SMS_EXEC_KDOPEN;
        break;

      case  ClosingTaskList:
        p->execCmd = SMS_EXEC_KDCLOSE;
        break;

      case  LaneAssign:
        p->execCmd = SMS_EXEC_LANEASSG;
        break;

      case  PriorityAssignment:
        p->execCmd = SMS_EXEC_PASSIGN;
        break;

      case  ManualEntryPayrollHours:
        p->execCmd = SMS_EXEC_KPRMAN;
        break;

      case  ReprintEodPayrollRpt:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = QStringLiteral( " prdaily.rpt" );
        break;

      case  ReprintEowPayrollRpt:
        p->execCmd = SMS_EXEC_KRPTUTIL;
        p->appArgs = QStringLiteral( " prweekly.rpt" );
        break;

      case  PayrollHistoryRpt:
        p->execCmd = SMS_EXEC_KPRHIST;
        break;

      default:
      case  NoApp:
        QMessageBox::critical( nullptr,
                               tr( "Error" ),
                               tr( "An Unknown Application was Requested" ) );
        return  false;
    }

    qDebug() << "Application is"  << p->execCmd;

    p->envStr  = QStringLiteral( "TERM=vt10S" );
    p->envStr += QStringLiteral( ";DEVTTY=/dev/tty" ) + znc->devTTY();
    p->envStr += QStringLiteral( ";LANG=" ) + znc->localeName();
    p->envStr += QStringLiteral( ";ZN_TERM=1" );
    p->envStr += QStringLiteral( ";CLIENT_IP=" ) + znc->ipAddress();

    if (!commInit())
    {
        qDebug() << "Failed to initialize I/O";
        return  false;
    }

    ZnetCommon::grabShortcuts();

    return  true;
}

//============================================================================
//  P U B L I C   S L O T S   I N T E R F A C E
//============================================================================

//----------------------------------------------------------------------------

int  GreenScreen::exec()
{
    centerDialog();

    return  QDialog::exec();
}

//----------------------------------------------------------------------------

void  GreenScreen::processScannerData( const QString& data )
{
    if (!data.isEmpty())
    {
        scannerReadCB( data );
    }
}

//============================================================================
//  P U B L I C   S T A T I C   I N T E R F A C E
//============================================================================

//----------------------------------------------------------------------------

GreenScreen::AppType  GreenScreen::appTypeFromString( const QString& name )
{
    return  APP_TYPE_MAP.value( name, AppType::NoApp );
}

//============================================================================
//  P R I V A T E   I N T E R F A C E
//============================================================================

//----------------------------------------------------------------------------

bool  GreenScreen::commInit()
{
    qDebug() << "Connection to:" << p->commHost << p->commPort;

    p->commDevice.connectToHost( p->commHost, p->commPort );
    if (!p->commDevice.waitForConnected( DEFAULT_COMM_WAIT_MS ))
    {
        qWarning() << "Unable to connect to term server!";
        return  false;
    }

    ZnetCommon::setTerminalRunning( true );

    if (!commSend( p->execCmd ))
    {
        return  false;
    }

    if (!commSend( p->execPath ))
    {
        return  false;
    }

    if (!commSend( p->appArgs ))
    {
        return  false;
    }

    if (!commSend( p->authKey ))
    {
        return  false;
    }

    if (!commSend( p->envStr ))
    {
        return  false;
    }

    quint32  linger = 0u;
    QByteArray  slinger( reinterpret_cast<char*>( &linger ), sizeof(linger) );
    if (p->commDevice.write( slinger ) <= 0)
    {
        return  false;
    }

    p->commDevice.flush();

    return true;

}

//----------------------------------------------------------------------------

bool  GreenScreen::commSend( const QString& string )
{
    quint32  len = htonl( quint32( string.length() ) );
    QByteArray  slen( reinterpret_cast<char*>( &len ), sizeof(len) );

    if (p->commDevice.write( slen ) <= 0)
    {
        return  false;
    }

    if (!string.isEmpty())
    {
        if (p->commDevice.write( string.toLatin1() ) <= 0)
        {
            return  false;
        }
    }

    return  true;
}

//----------------------------------------------------------------------------

void  GreenScreen::centerDialog()
{
    QWidget* pw = qobject_cast<QWidget*>( parent() );
    if (pw)
    {
        QRect  pwrect = pw->frameGeometry();
        QRect  myrect = this->frameGeometry();
        myrect.moveCenter( pwrect.center() );
        move( myrect.topLeft() );
    }
}

//----------------------------------------------------------------------------

void  GreenScreen::resizeEvent( QResizeEvent* event )
{
    Q_UNUSED( event )

    centerDialog();
}

//----------------------------------------------------------------------------

void  GreenScreen::showEvent( QShowEvent* event )
{
    Q_UNUSED( event )

    centerDialog();
}

//============================================================================
//  P R I V A T E   S L O T S   I N T E R F A C E
//============================================================================

//----------------------------------------------------------------------------

void  GreenScreen::commReadCB()
{
    while (p->commDevice.bytesAvailable())
    {
        QByteArray  data;
        data = p->commDevice.readAll();
        p->terminal->receiveData( data );
    }
}

//----------------------------------------------------------------------------

void  GreenScreen::commWriteCB( const QByteArray& data )
{
    p->commDevice.write( data );
}

//----------------------------------------------------------------------------

void  GreenScreen::sessionDoneCB()
{
    qDebug() << "Terminal session finished.";

    ZnetCommon::releaseShortcuts();
    ZnetCommon::setTerminalRunning( false );

    emit  sessionEnded();
    accept();
}

//----------------------------------------------------------------------------

void  GreenScreen::cashDrawerAvailCB( bool state )
{
    bool  isreg = ZnetCommon::znetCommon()->isRegister();

    ZnetCommon::setIsRegisterWithTillState( (state && isreg) );
}

//----------------------------------------------------------------------------

void  GreenScreen::scannerReadCB( const QString& data )
{
    QWidget*  activeWidget = qApp->focusWidget();
    if (!activeWidget)
    {
        qDebug() << "using window";
        activeWidget = qApp->activeWindow();
    }

    if (!activeWidget)
    {
        qDebug() << "No active window found";
        return;
    }

    const int  scandatalen = data.length();
    qDebug() << "sending the data:" << data << activeWidget->windowTitle();

    // Create and send the character events
    int  oc  {};
    int  uc  {};
    for (int i=0; i<scandatalen; ++i)
    {
        const QChar  curKey = data[i];          // the current character
        QString      keyStr = curKey;           // the string version of the current character
        oc = curKey.toLatin1();            // the original character
        uc = curKey.toUpper().toLatin1();  // the upper case version
        Qt::KeyboardModifier  state;            // the modifier button state

        // the printable characters
        if (uc >= Qt::Key_Space)
        {
            // if it's a letter and the upper case character matches the
            // original, then simulate the shift key being held
            state = ((curKey.isLetter() && (uc == oc))) ?  Qt::ShiftModifier : Qt::NoModifier;

            QApplication::postEvent( activeWidget,
                                     new QKeyEvent( QEvent::KeyPress, oc, state, keyStr )) ;
        }
        else
        {
            // handle a few of the special characters
            switch (oc)
            {
              case '\n':
                uc = Qt::Key_Enter;
                break;

              case '\t':
                uc = Qt::Key_Tab;
                break;

              case 0x1b:
                uc = Qt::Key_Escape;
                break;

              case 0x8:
                uc = Qt::Key_Backspace;
                break;

              default:
                uc = 0;
            }

            if (uc != 0)
            {
                QApplication::postEvent( activeWidget,
                                         new QKeyEvent( QEvent::KeyPress, oc, Qt::NoModifier, keyStr ) );
            }
        }
    }

    if ((uc != Qt::Key_Return) && (uc != Qt::Key_Enter))
    {
        QApplication::postEvent( activeWidget,
                                 new QKeyEvent( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\n" ) );
    }
}
