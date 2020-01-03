// Copyright 2013 Velodyne Acoustics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// Copyright 2013 Velodyne Acoustics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vvMainWindow.h"
#include "ui_vvMainWindow.h"
#include "vvLoadDataReaction.h"

#include <vtkSMProxyManager.h>
#include <vtkSMSessionProxyManager.h>

#include <pqActiveObjects.h>
#include <pqApplicationCore.h>
#include <pqInterfaceTracker.h>
#include <pqObjectBuilder.h>
#include <pqPythonShellReaction.h>
#include <pqQtMessageHandlerBehavior.h>
#include <pqRenderView.h>
#include <pqRenderViewSelectionReaction.h>
#include <pqDeleteReaction.h>
#include <pqHelpReaction.h>
#include <pqServer.h>
#include <pqSettings.h>
#include <pqSpreadSheetView.h>
#include <pqSpreadSheetViewModel.h>
#include <pqSpreadSheetViewDecorator.h>
#include <pqStandardPropertyWidgetInterface.h>
#include <pqStandardViewFrameActionsImplementation.h>
#include <pqLidarViewManager.h>
#include <pqParaViewMenuBuilders.h>
#include <pqPythonManager.h>
#include <pqTabbedMultiViewWidget.h>
#include <pqSetName.h>
#include <vtkPVPlugin.h>
#include <vtkSMPropertyHelper.h>
#include "pqAxesToolbar.h"
#include "pqCameraToolbar.h"
#include <pqParaViewBehaviors.h>

#include <QToolBar>
#include <QShortcut>
#include <QMenu>
#include <QMimeData>
#include <QUrl>

#include <cassert>
#include <iostream>
#include <sstream>

#include "vvPlayerControlsToolbar.h"

// Declare the plugin to load.
PV_PLUGIN_IMPORT_INIT(LidarPlugin);
PV_PLUGIN_IMPORT_INIT(PythonQtPlugin);
PV_PLUGIN_IMPORT_INIT(VelodynePlugin);

class vvMainWindow::pqInternals
{
public:
  pqInternals(vvMainWindow* window)
    : Ui()
    , MainView(0)
  {
    this->Ui.setupUi(window);
    this->paraviewInit(window);
    this->setupUi();

    window->show();
    window->raise();
    window->activateWindow();
  }
  Ui::vvMainWindow Ui;
  pqRenderView* MainView = nullptr;
  pqServer* Server = nullptr;
  pqObjectBuilder* Builder = nullptr;
  pqSpreadSheetView* SpreadSheetView = nullptr;
  pqSpreadSheetViewDecorator* SpreadSheetViewDec = nullptr;

private:
  void paraviewInit(vvMainWindow* window)
  {
    pqApplicationCore* core = pqApplicationCore::instance();

    // need to be created before the first scene
    QToolBar* vcrToolbar = new vvPlayerControlsToolbar(window)
      << pqSetName("Player Control");
    window->addToolBar(Qt::TopToolBarArea, vcrToolbar);

    QToolBar* cameraToolbar = new pqCameraToolbar(window)
      << pqSetName("cameraToolbar");
    window->addToolBar(Qt::TopToolBarArea, cameraToolbar);

    QToolBar* axesToolbar = new pqAxesToolbar(window)
      << pqSetName("axesToolbar");
    window->addToolBar(Qt::TopToolBarArea, axesToolbar);

    // Give the macros menu to the pqPythonMacroSupervisor
    pqPythonManager* manager =
      qobject_cast<pqPythonManager*>(pqApplicationCore::instance()->manager("PYTHON_MANAGER"));
    if (manager)
    {
      QToolBar* macrosToolbar = new QToolBar("Macros Toolbars", window)
        << pqSetName("MacrosToolbar");
      manager->addWidgetForRunMacros(macrosToolbar);
      window->addToolBar(Qt::TopToolBarArea, macrosToolbar);
    }

    // Register ParaView interfaces.
    pqInterfaceTracker* pgm = core->interfaceTracker();
    //    pgm->addInterface(new pqStandardViewModules(pgm));
    pgm->addInterface(new pqStandardPropertyWidgetInterface(pgm));
    pgm->addInterface(new pqStandardViewFrameActionsImplementation(pgm));

    // Define application behaviors.
    new pqQtMessageHandlerBehavior(window);

    pqParaViewBehaviors::enableQuickLaunchShortcuts();
    pqParaViewBehaviors::enableSpreadSheetVisibilityBehavior();
    pqParaViewBehaviors::enableObjectPickingBehavior();
    pqParaViewBehaviors::enableCrashRecoveryBehavior();
    pqParaViewBehaviors::enableAutoLoadPluginXMLBehavior();
    pqParaViewBehaviors::enableDataTimeStepBehavior();
    pqParaViewBehaviors::enableCommandLineOptionsBehavior();
    pqParaViewBehaviors::enableLiveSourceBehavior();
    pqParaViewBehaviors::enableApplyBehavior();

    // Check if the settings are well formed i.e. if an OriginalMainWindow
    // state was previously saved. If not, we don't want to automatically
    // restore the settings state nor save it on quitting LidarView.
    // An OriginalMainWindow state will be force saved once the UI is completly
    // set up.
    pqSettings* const settings = pqApplicationCore::instance()->settings();
    bool shouldClearSettings = false;
    QStringList keys = settings->allKeys();

    if (keys.size() == 0)
    {
      // There were no settings before, let's save the current state as
      // OriginalMainWindow state
      shouldClearSettings = true;
    }
    else
    {
      // Checks if the existing settings are well formed and if not, clear them.
      // An original MainWindow state will be force saved later once the UI is
      // entirely set up
      for (int keyIndex = 0; keyIndex < keys.size(); ++keyIndex)
      {
        if (keys[keyIndex].contains("OriginalMainWindow"))
        {
          shouldClearSettings = true;
          break;
        }
      }
    }

    if (shouldClearSettings)
    {
      pqParaViewBehaviors::enablePersistentMainWindowStateBehavior();
    }
    else
    {
      if (keys.size() > 0)
      {
        vtkGenericWarningMacro("Settings weren't set correctly. Clearing settings.")
      }

      // As pqPersistentMainWindowStateBehavior is not created right now,
      // we can clear the settings as the current bad state won't be saved on
      // closing LidarView
      settings->clear();
    }

    // Connect to builtin server.
    this->Builder = core->getObjectBuilder();
    this->Server = this->Builder->createServer(pqServerResource("builtin:"));
    pqActiveObjects::instance().setActiveServer(this->Server);

    // Set default render view settings
    vtkSMSessionProxyManager* pxm =
      vtkSMProxyManager::GetProxyManager()->GetActiveSessionProxyManager();
    vtkSMProxy* renderviewsettings = pxm->GetProxy("RenderViewSettings");
    assert(renderviewsettings);

    vtkSMPropertyHelper(renderviewsettings, "ResolveCoincidentTopology").Set(0);

    // Set the central widget
    pqTabbedMultiViewWidget* mv = new pqTabbedMultiViewWidget;
    mv->setTabVisibility(false);
    window->setCentralWidget(mv);

    QObject::connect(this->Ui.actionSpreadsheet, SIGNAL(toggled(bool)), window, SLOT(onToggleSpreadSheet(bool)));

    // This ensures that closing the dock widget toggles the spreadsheet to off
    for (QObject* child : this->Ui.spreadSheetDock->children())
    {
      if (child->objectName() == "qt_dockwidget_closebutton")
      {
        if (QWidget* button = dynamic_cast<QWidget* >(child))
        {
          button->disconnect();
          QObject::connect(button, SIGNAL(clicked()), this->Ui.actionSpreadsheet, SLOT(toggle()));
        }
      }
    }


    this->MainView =
      qobject_cast<pqRenderView*>(this->Builder->createView(pqRenderView::renderViewType(), this->Server));
    assert(this->MainView);

    vtkSMPropertyHelper(this->MainView->getProxy(), "CenterAxesVisibility").Set(0);
    double bgcolor[3] = { 0, 0, 0 };
    vtkSMPropertyHelper(this->MainView->getProxy(), "Background").Set(bgcolor, 3);
    // MultiSamples doesn't work, we need to set that up before registering the proxy.
    // vtkSMPropertyHelper(view->getProxy(),"MultiSamples").Set(1);
    this->MainView->getProxy()->UpdateVTKObjects();

    // Specify each Properties Panel as we do want to present one panel per dock
    this->Ui.propertiesPanel->setPanelMode(pqPropertiesPanel::SOURCE_PROPERTIES);
    this->Ui.viewPropertiesPanel->setPanelMode(pqPropertiesPanel::VIEW_PROPERTIES);
    this->Ui.displayPropertiesPanel->setPanelMode(pqPropertiesPanel::DISPLAY_PROPERTIES);

    // Enable help from the properties panel.
    QObject::connect(this->Ui.propertiesPanel,
      SIGNAL(helpRequested(const QString&, const QString&)),
      window, SLOT(showHelpForProxy(const QString&, const QString&)));

    /// hook delete to pqDeleteReaction.
    QAction* tempDeleteAction = new QAction(window);
    pqDeleteReaction* handler = new pqDeleteReaction(tempDeleteAction);
    handler->connect(this->Ui.propertiesPanel,
      SIGNAL(deleteRequested(pqPipelineSource*)),
      SLOT(deleteSource(pqPipelineSource*)));

    // specify how corner are occupied by the dockable widget
    window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // organize dockable widget in tab
    window->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    window->tabifyDockWidget(this->Ui.propertiesDock, this->Ui.colorMapEditorDock);
    window->tabifyDockWidget(this->Ui.viewPropertiesDock, this->Ui.colorMapEditorDock);
    window->tabifyDockWidget(this->Ui.displayPropertiesDock, this->Ui.colorMapEditorDock);
    window->tabifyDockWidget(this->Ui.spreadSheetDock, this->Ui.informationDock);
    window->tabifyDockWidget(this->Ui.spreadSheetDock, this->Ui.memoryInspectorDock);
    window->tabifyDockWidget(this->Ui.spreadSheetDock, this->Ui.viewAnimationDock);

    // hide docker by default
    this->Ui.pipelineBrowserDock->hide();
    this->Ui.propertiesDock->hide();
    this->Ui.viewPropertiesDock->hide();
    this->Ui.displayPropertiesDock->hide();
    this->Ui.colorMapEditorDock->hide();
    this->Ui.spreadSheetDock->hide();
    this->Ui.informationDock->hide();
    this->Ui.memoryInspectorDock->hide();
    this->Ui.viewAnimationDock->hide();

    // Setup the View menu. This must be setup after all toolbars and dockwidgets
    // have been created.
    pqParaViewMenuBuilders::buildViewMenu(*this->Ui.menuViews, *window);

    /// If you want to automatically add a menu for sources as requested in the
    /// configuration pass in a non-null main window.
    pqParaViewMenuBuilders::buildSourcesMenu(*this->Ui.menuSources, nullptr);

    /// If you want to automatically add a menu for filters as requested in the
    /// configuration pass in a non-null main window.
    pqParaViewMenuBuilders::buildFiltersMenu(*this->Ui.menuFilters, nullptr);

    // setup the context menu for the pipeline browser.
    pqParaViewMenuBuilders::buildPipelineBrowserContextMenu(*this->Ui.pipelineBrowser);

    // build Paraview file menu
    QMenu *paraviewFileMenu = this->Ui.menuAdvance->addMenu("File (Paraview)");
    pqParaViewMenuBuilders::buildFileMenu(*paraviewFileMenu);
    // for some reason the menu builder rename the QMenu...
    paraviewFileMenu->setTitle("File (Paraview)");

    // build Paraview edit menu
    QMenu *paraviewEditMenu = this->Ui.menuAdvance->addMenu("Edit (Paraview)");
    // for some reason the menu builder rename the QMenu...
    pqParaViewMenuBuilders::buildEditMenu(*paraviewEditMenu);
    paraviewEditMenu->setTitle("Edit (Paraview)");

    // build Paraview tools menu
    QMenu *paraviewToolsMenu = this->Ui.menuAdvance->addMenu("Tools (Paraview)");
    pqParaViewMenuBuilders::buildToolsMenu(*paraviewToolsMenu);

    // build Paraview macro menu
    QMenu *paraviewMacroMenu = this->Ui.menuAdvance->addMenu("Macro (Paraview)");
    pqParaViewMenuBuilders::buildMacrosMenu(*paraviewMacroMenu);

    new pqParaViewBehaviors(window, window);

    pqActiveObjects::instance().setActiveView(this->MainView);
  }

  //-----------------------------------------------------------------------------
  void setupUi()
  {
    new pqRenderViewSelectionReaction(this->Ui.actionSelect_Visible_Points, this->MainView,
      pqRenderViewSelectionReaction::SELECT_SURFACE_POINTS);
    new pqRenderViewSelectionReaction(this->Ui.actionSelect_All_Points, this->MainView,
      pqRenderViewSelectionReaction::SELECT_FRUSTUM_POINTS);

    new pqPythonShellReaction(this->Ui.actionPython_Console);

    pqLidarViewManager::instance()->setup();

    pqSettings* const settings = pqApplicationCore::instance()->settings();
    const QVariant& gridVisible =
      settings->value("LidarPlugin/MeasurementGrid/Visibility", true);
    this->Ui.actionMeasurement_Grid->setChecked(gridVisible.toBool());

    new vvLoadDataReaction(this->Ui.actionOpenPcap, false);

    connect(this->Ui.actionOpen_Sensor_Stream, SIGNAL(triggered()), pqLidarViewManager::instance(),
      SLOT(onOpenSensor()));

    connect(this->Ui.actionMeasurement_Grid, SIGNAL(toggled(bool)), pqLidarViewManager::instance(),
      SLOT(onMeasurementGrid(bool)));

    connect(this->Ui.actionResetDefaultSettings, SIGNAL(triggered()),
      pqLidarViewManager::instance(), SLOT(onResetDefaultSettings()));

    connect(this->Ui.actionShowErrorDialog, SIGNAL(triggered()), pqApplicationCore::instance(),
      SLOT(showOutputWindow()));
  }
};

//-----------------------------------------------------------------------------
vvMainWindow::vvMainWindow()
  : Internals(new vvMainWindow::pqInternals(this))
{
  pqApplicationCore::instance()->registerManager(
    "COLOR_EDITOR_PANEL", this->Internals->Ui.colorMapEditorDock);
  this->Internals->Ui.colorMapEditorDock->hide();

  PV_PLUGIN_IMPORT(LidarPlugin);
  PV_PLUGIN_IMPORT(PythonQtPlugin);
  PV_PLUGIN_IMPORT(VelodynePlugin);

  // Branding
  std::stringstream ss;
  ss << "Reset " << SOFTWARE_NAME << " settings";
  QString text = QString(ss.str().c_str());
  this->Internals->Ui.actionResetDefaultSettings->setText(text);
  ss.str("");
  ss.clear();

  ss << "This will reset all " << SOFTWARE_NAME << " settings by default";
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionResetDefaultSettings->setIconText(text);
  ss.str("");
  ss.clear();

  ss << "About " << SOFTWARE_NAME;
  text = QString(ss.str().c_str());
  this->Internals->Ui.actionAbout_LidarView->setText(text);
  ss.str("");
  ss.clear();
}

//-----------------------------------------------------------------------------
vvMainWindow::~vvMainWindow()
{
  if (isSpreadSheetOpen())
  {
    this->destructSpreadSheet();
  }
  delete this->Internals;
  this->Internals = NULL;
}

//-----------------------------------------------------------------------------
void vvMainWindow::dragEnterEvent(QDragEnterEvent* evt)
{
  evt->acceptProposedAction();
}

//-----------------------------------------------------------------------------
void vvMainWindow::dropEvent(QDropEvent* evt)
{
  QList<QUrl> urls = evt->mimeData()->urls();
  if (urls.isEmpty())
  {
    return;
  }

  QList<QString> files;

  foreach (QUrl url, urls)
  {
    if (!url.toLocalFile().isEmpty())
    {
      files.append(url.toLocalFile());
    }
  }

  // If we have no file we return
  if (files.empty() || files.first().isEmpty())
  {
    return;
  }

  if (files[0].endsWith(".pcap"))
  {
    pqLidarViewManager::instance()->runPython(QString("lv.openPCAP('" + files[0] + "')"));
  }
  else {
    pqLoadDataReaction::loadData(files);
  }
}

//-----------------------------------------------------------------------------
void vvMainWindow::showHelpForProxy(const QString& groupname, const
  QString& proxyname)
{
  pqHelpReaction::showProxyHelp(groupname, proxyname);
}

void vvMainWindow::constructSpreadSheet()
{
  assert(this->Internals->SpreadSheetView == nullptr);
  this->Internals->SpreadSheetView = qobject_cast<pqSpreadSheetView*>
      (this->Internals->Builder->createView(pqSpreadSheetView::spreadsheetViewType(), this->Internals->Server, true));
  this->Internals->SpreadSheetView->rename("main spreadsheet view");
  assert(this->Internals->SpreadSheetView != nullptr);

  this->Internals->SpreadSheetViewDec = new pqSpreadSheetViewDecorator(this->Internals->SpreadSheetView);
  this->Internals->SpreadSheetViewDec->setPrecision(3);
  this->Internals->SpreadSheetViewDec->setFixedRepresentation(true);

  this->Internals->Ui.spreadSheetDock->setWidget(this->Internals->SpreadSheetView->widget());
  this->Internals->SpreadSheetView->getProxy()->UpdateVTKObjects();
  this->Internals->Ui.spreadSheetDock->setVisible(true);

  this->Internals->SpreadSheetView->getViewModel()->setVisible(0, false);
  this->Internals->SpreadSheetView->getViewModel()->setVisible(2, false);
}

//-----------------------------------------------------------------------------
void vvMainWindow::destructSpreadSheet()
{
  pqActiveObjects::instance().setActiveView(this->Internals->MainView);

  this->Internals->Ui.spreadSheetDock->setVisible(false);
  this->Internals->Ui.spreadSheetDock->setWidget(nullptr);

  delete this->Internals->SpreadSheetViewDec;
  this->Internals->SpreadSheetViewDec = nullptr;

  delete this->Internals->SpreadSheetView;
  this->Internals->SpreadSheetView = nullptr;
}

//-----------------------------------------------------------------------------
void vvMainWindow::onToggleSpreadSheet(bool toggled)
{
  if (toggled)
  {
    this->constructSpreadSheet();
    emit this->spreadSheetEnabled(true);
  }
  else
  {
    this->destructSpreadSheet();
    emit this->spreadSheetEnabled(false);
  }
}

//-----------------------------------------------------------------------------
bool vvMainWindow::isSpreadSheetOpen()
{
  return this->Internals->SpreadSheetView != nullptr;
}
