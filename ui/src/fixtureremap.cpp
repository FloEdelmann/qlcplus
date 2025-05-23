/*
  Q Light Controller Plus
  fixtureremap.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QXmlStreamReader>
#include <QProgressDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QDebug>
#include <QDir>
#include <QSettings>

#include "monitorproperties.h"
#include "vcaudiotriggers.h"
#include "virtualconsole.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "channelsgroup.h"
#include "fixtureremap.h"
#include "remapwidget.h"
#include "qlcchannel.h"
#include "addfixture.h"
#include "efxfixture.h"
#include "scenevalue.h"
#include "chaserstep.h"
#include "audiobar.h"
#include "vcslider.h"
#include "sequence.h"
#include "qlcfile.h"
#include "vcxypad.h"
#include "vcframe.h"
#include "chaser.h"
#include "scene.h"
#include "efx.h"
#include "doc.h"
#include "app.h"

#define KColumnName         0
#define KColumnAddress      1
#define KColumnUniverse     2
#define KColumnID           3
#define KColumnChIdx        4

#define SETTINGS_GEOMETRY "fixturemap/geometry"

FixtureRemap::FixtureRemap(Doc *doc, QWidget *parent)
    : QDialog(parent)
    , m_doc(doc)
{
    Q_ASSERT(doc != NULL);

    setupUi(this);

    QSettings settings;
    QVariant geometrySettings = settings.value(SETTINGS_GEOMETRY);
    if (geometrySettings.isValid() == true)
        restoreGeometry(geometrySettings.toByteArray());

    connect(m_importButton, SIGNAL(clicked()),
            this, SLOT(slotImportFixtures()));
    connect(m_addButton, SIGNAL(clicked()),
            this, SLOT(slotAddTargetFixture()));
    connect(m_removeButton, SIGNAL(clicked()),
            this, SLOT(slotRemoveTargetFixture()));
    connect(m_cloneButton, SIGNAL(clicked()),
            this, SLOT(slotCloneSourceFixture()));
    connect(m_remapButton, SIGNAL(clicked()),
            this, SLOT(slotAddRemap()));
    connect(m_unmapButton, SIGNAL(clicked()),
            this, SLOT(slotRemoveRemap()));

    m_cloneButton->setEnabled(false);

    remapWidget = new RemapWidget(m_sourceTree, m_targetTree, this);
    remapWidget->show();
    m_remapLayout->addWidget(remapWidget);

    m_targetDoc = new Doc(this);
    /* Load user fixtures first so that they override system fixtures */
    m_targetDoc->fixtureDefCache()->load(QLCFixtureDefCache::userDefinitionDirectory());
    m_targetDoc->fixtureDefCache()->loadMap(QLCFixtureDefCache::systemDefinitionDirectory());

    /* Remove the default set of universes from the target Doc and re-fill it
     * with the current Doc universe list */
    m_targetDoc->inputOutputMap()->removeAllUniverses();

    int index = 0;
    foreach (Universe *uni, m_doc->inputOutputMap()->universes())
    {
        m_targetDoc->inputOutputMap()->addUniverse(uni->id());
        m_targetDoc->inputOutputMap()->setUniverseName(index, uni->name());
        m_targetDoc->inputOutputMap()->startUniverses();
        index++;
    }

    m_sourceTree->setIconSize(QSize(24, 24));
    m_sourceTree->setAllColumnsShowFocus(true);
    fillFixturesTree(m_doc, m_sourceTree);

    m_targetTree->setIconSize(QSize(24, 24));
    m_targetTree->setAllColumnsShowFocus(true);

    connect(m_sourceTree->verticalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(slotUpdateConnections()));
    connect(m_sourceTree, SIGNAL(clicked(QModelIndex)),
            this, SLOT(slotUpdateConnections()));
    connect(m_sourceTree, SIGNAL(expanded(QModelIndex)),
            this, SLOT(slotUpdateConnections()));
    connect(m_sourceTree, SIGNAL(collapsed(QModelIndex)),
            this, SLOT(slotUpdateConnections()));
    connect(m_sourceTree, SIGNAL(itemSelectionChanged()),
            this, SLOT(slotSourceSelectionChanged()));

    connect(m_targetTree->verticalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(slotUpdateConnections()));
    connect(m_targetTree, SIGNAL(clicked(QModelIndex)),
            this, SLOT(slotUpdateConnections()));
    connect(m_targetTree, SIGNAL(expanded(QModelIndex)),
            this, SLOT(slotUpdateConnections()));
    connect(m_targetTree, SIGNAL(collapsed(QModelIndex)),
            this, SLOT(slotUpdateConnections()));

    // retrieve the original project name from the QLC+ App class
    App *mainApp = (App *)m_doc->parent();
    QString prjName = mainApp->fileName();

    if (prjName.lastIndexOf(".") > 0)
        prjName.insert(prjName.lastIndexOf("."), tr(" (remapped)"));
    else
        prjName.append(tr(" (remapped)"));

    m_targetProjectLabel->setText(prjName);
}

FixtureRemap::~FixtureRemap()
{
    QSettings settings;
    settings.setValue(SETTINGS_GEOMETRY, saveGeometry());

    delete m_targetDoc;
}

QTreeWidgetItem *FixtureRemap::getUniverseItem(Doc *doc, quint32 universe, QTreeWidget *tree)
{
    QTreeWidgetItem *topItem = NULL;

    for (int i = 0; i < tree->topLevelItemCount(); i++)
    {
        QTreeWidgetItem* tItem = tree->topLevelItem(i);
        quint32 tUni = tItem->text(KColumnUniverse).toUInt();
        if (tUni == universe)
        {
            topItem = tItem;
            break;
        }
    }

    // Haven't found this universe node ? Create it.
    if (topItem == NULL)
    {
        topItem = new QTreeWidgetItem(tree);
        topItem->setText(KColumnName, doc->inputOutputMap()->universes().at(universe)->name());
        topItem->setText(KColumnUniverse, QString::number(universe));
        topItem->setText(KColumnID, QString::number(Function::invalidId()));
        topItem->setExpanded(true);
    }

    return topItem;
}

void FixtureRemap::fillFixturesTree(Doc *doc, QTreeWidget *tree)
{
    foreach (Fixture *fxi, doc->fixtures())
    {
        quint32 uni = fxi->universe();
        QTreeWidgetItem *topItem = getUniverseItem(doc, uni, tree);

        quint32 baseAddr = fxi->address();
        QTreeWidgetItem *fItem = new QTreeWidgetItem(topItem);
        fItem->setText(KColumnName, fxi->name());
        fItem->setIcon(KColumnName, fxi->getIconFromType());
        fItem->setText(KColumnAddress, QString("%1 - %2").arg(baseAddr + 1).arg(baseAddr + fxi->channels()));
        fItem->setText(KColumnUniverse, QString::number(uni));
        fItem->setText(KColumnID, QString::number(fxi->id()));

        for (quint32 c = 0; c < fxi->channels(); c++)
        {
            const QLCChannel* channel = fxi->channel(c);
            QTreeWidgetItem *item = new QTreeWidgetItem(fItem);
            item->setText(KColumnName, QString("%1:%2").arg(c + 1)
                          .arg(channel->name()));
            item->setIcon(KColumnName, channel->getIcon());
            item->setText(KColumnUniverse, QString::number(uni));
            item->setText(KColumnID, QString::number(fxi->id()));
            item->setText(KColumnChIdx, QString::number(c));
        }
    }

    tree->resizeColumnToContents(KColumnName);
}

QString FixtureRemap::createImportDialog()
{
    QString fileName;

    /* Create a file save dialog */
    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Import Fixtures List"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);

    /* Append file filters to the dialog */
    QStringList filters;
    filters << tr("Fixtures List (*%1)").arg(KExtFixtureList);
#if defined(WIN32) || defined(Q_OS_WIN)
    filters << tr("All Files (*.*)");
#else
    filters << tr("All Files (*)");
#endif
    dialog.setNameFilters(filters);

    /* Append useful URLs to the dialog */
    QList <QUrl> sidebar;
    sidebar.append(QUrl::fromLocalFile(QDir::homePath()));
    sidebar.append(QUrl::fromLocalFile(QDir::rootPath()));
    dialog.setSidebarUrls(sidebar);

    /* Get file name */
    if (dialog.exec() != QDialog::Accepted)
        return "";

    fileName = dialog.selectedFiles().first();
    if (fileName.isEmpty() == true)
        return "";

    return fileName;
}

void FixtureRemap::slotImportFixtures()
{
    QString fileName = createImportDialog();

    QMessageBox msgBox;
    msgBox.setText(tr("Do you want to automatically connect fixtures with the same name?"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    bool autoConnect = msgBox.exec() == QMessageBox::Yes ? true : false;

    QXmlStreamReader *doc = QLCFile::getXMLReader(fileName);
    if (doc == NULL || doc->device() == NULL || doc->hasError())
    {
        qWarning() << Q_FUNC_INFO << "Unable to read from" << fileName;
        return;
    }

    while (!doc->atEnd())
    {
        if (doc->readNext() == QXmlStreamReader::DTD)
            break;
    }
    if (doc->hasError())
    {
        QLCFile::releaseXMLReader(doc);
        return;
    }

    if (doc->dtdName() == KXMLQLCFixturesList)
    {
        doc->readNextStartElement();
        if (doc->name() != KXMLQLCFixturesList)
        {
            qWarning() << Q_FUNC_INFO << "Fixture Definition node not found";
            QLCFile::releaseXMLReader(doc);
            return;
        }

        while (doc->readNextStartElement())
        {
            if (doc->name() == KXMLFixture)
            {
                Fixture* fxi = new Fixture(m_targetDoc);
                Q_ASSERT(fxi != NULL);

                if (fxi->loadXML(*doc, m_targetDoc, m_doc->fixtureDefCache()) == true)
                {
                    if (m_targetDoc->addFixture(fxi) == false)
                    {
                        qWarning() << Q_FUNC_INFO << "Fixture" << fxi->name() << "cannot be created.";
                        delete fxi;
                    }
                }
                else
                {
                    qWarning() << Q_FUNC_INFO << "Fixture" << fxi->name() << "cannot be loaded.";
                    delete fxi;
                }
            }
            else if (doc->name() == KXMLQLCFixtureGroup)
            {
                FixtureGroup* grp = new FixtureGroup(m_targetDoc);
                Q_ASSERT(grp != NULL);

                if (grp->loadXML(*doc) == true)
                {
                    m_targetDoc->addFixtureGroup(grp, grp->id());
                }
                else
                {
                    qWarning() << Q_FUNC_INFO << "FixtureGroup" << grp->name() << "cannot be loaded.";
                    delete grp;
                }
            }
            else
            {
                qWarning() << Q_FUNC_INFO << "Unknown label tag:" << doc->name().toString();
                doc->skipCurrentElement();
            }
        }
        fillFixturesTree(m_targetDoc, m_targetTree);

        if (autoConnect)
        {
            for (int tu = 0; tu < m_targetTree->topLevelItemCount(); tu++)
            {
                QTreeWidgetItem *tgtUniItem = m_targetTree->topLevelItem(tu);

                for (int ti = 0; ti < tgtUniItem->childCount(); ti++)
                {
                    QTreeWidgetItem *tgtItem = tgtUniItem->child(ti);

                    for (int su = 0; su < m_sourceTree->topLevelItemCount(); su++)
                    {
                        QTreeWidgetItem *srcUniItem = m_sourceTree->topLevelItem(su);

                        for (int si = 0; si < srcUniItem->childCount(); si++)
                        {
                            QTreeWidgetItem *srcItem = srcUniItem->child(si);

                            if (srcItem->text(KColumnName) == tgtItem->text(KColumnName))
                            {
                                connectFixtures(srcItem, tgtItem);
                                break;
                            }
                        }
                    }
                }
            }
            remapWidget->setRemapList(m_remapList);
        }
    }
    QLCFile::releaseXMLReader(doc);
}

void FixtureRemap::slotAddTargetFixture()
{
    AddFixture af(this, m_targetDoc);
    if (af.exec() == QDialog::Rejected)
        return;

    QString name = af.name();
    quint32 address = af.address();
    quint32 universe = af.universe();
    quint32 channels = af.channels();
    QLCFixtureDef* fixtureDef = af.fixtureDef();
    QLCFixtureMode* mode = af.mode();
    int gap = af.gap();

    for (int i = 0; i < af.amount(); i++)
    {
        QString modname;

        /* If an empty name was given use the model instead */
        if (name.simplified().isEmpty())
        {
            if (fixtureDef != NULL)
                name = fixtureDef->model();
            else
                name = tr("Generic Dimmer");
        }

        /* If we're adding more than one fixture,
           append a number to the end of the name */
        if (af.amount() > 1)
            modname = QString("%1 #%2").arg(name).arg(i+1);
        else
            modname = name;

        /* Create the target fixture */
        Fixture* fxi = new Fixture(m_targetDoc);

        /* Add the first fixture without gap, at the given address */
        fxi->setAddress(address + (i * channels) + (i * gap));
        fxi->setUniverse(universe);
        fxi->setName(modname);

        /* Set a fixture definition & mode if they were selected.
           Otherwise assign channels to a generic dimmer. */
        if (fixtureDef != NULL && mode != NULL)
            fxi->setFixtureDefinition(fixtureDef, mode);
        else
        {
            fixtureDef = fxi->genericDimmerDef(channels);
            mode = fxi->genericDimmerMode(fixtureDef, channels);
            fxi->setFixtureDefinition(fixtureDef, mode);
            //fxi->setChannels(channels);
        }

        m_targetDoc->addFixture(fxi);

        QTreeWidgetItem *topItem = getUniverseItem(m_targetDoc, universe, m_targetTree);

        quint32 baseAddr = fxi->address();
        QTreeWidgetItem *fItem = new QTreeWidgetItem(topItem);
        fItem->setText(KColumnName, fxi->name());
        fItem->setIcon(KColumnName, fxi->getIconFromType());
        fItem->setText(KColumnAddress, QString("%1 - %2").arg(baseAddr + 1).arg(baseAddr + fxi->channels()));
        fItem->setText(KColumnUniverse, QString::number(universe));
        fItem->setText(KColumnID, QString::number(fxi->id()));

        for (quint32 c = 0; c < fxi->channels(); c++)
        {
            const QLCChannel* channel = fxi->channel(c);
            QTreeWidgetItem *item = new QTreeWidgetItem(fItem);
            item->setText(KColumnName, QString("%1:%2").arg(c + 1)
                          .arg(channel->name()));
            item->setIcon(KColumnName, channel->getIcon());
            item->setText(KColumnUniverse, QString::number(universe));
            item->setText(KColumnID, QString::number(fxi->id()));
            item->setText(KColumnChIdx, QString::number(c));
        }
    }
    m_targetTree->resizeColumnToContents(KColumnName);

    qDebug() << "Fixtures in target doc:" << m_targetDoc->fixtures().count();
}

void FixtureRemap::slotRemoveTargetFixture()
{
    if (m_targetTree->selectedItems().count() == 0)
        return;

    QTreeWidgetItem *item = m_targetTree->selectedItems().first();
    bool ok = false;
    quint32 fxid = item->text(KColumnID).toUInt(&ok);
    if (ok == false)
        return;

    // Ask before deletion
    if (QMessageBox::question(this, tr("Delete Fixtures"),
                              tr("Do you want to delete the selected items?"),
                              QMessageBox::Yes, QMessageBox::No) == QMessageBox::No)
    {
        return;
    }

    int i = 0;
    QListIterator <RemapInfo> it(m_remapList);
    while (it.hasNext() == true)
    {
        RemapInfo info = it.next();
        quint32 tgtID = info.target->text(KColumnID).toUInt();
        if (tgtID == fxid)
            m_remapList.takeAt(i);
        else
            i++;
    }
    remapWidget->setRemapList(m_remapList);
    m_targetDoc->deleteFixture(fxid);
    for (int i = 0; i < item->childCount(); i++)
    {
        QTreeWidgetItem *child = item->child(i);
        delete child;
    }
    delete item;
    m_targetTree->resizeColumnToContents(KColumnName);

    qDebug() << "Fixtures in target doc:" << m_targetDoc->fixtures().count();
}

void FixtureRemap::slotCloneSourceFixture()
{
    if (m_sourceTree->selectedItems().count() == 0)
        return; // popup here ??

    QTreeWidgetItem *sItem = m_sourceTree->selectedItems().first();
    quint32 fxID = sItem->text(KColumnID).toUInt();
    Fixture *srcFix = m_doc->fixture(fxID);
    if (srcFix == NULL)
        return; // popup here ?

    quint32 srcAddr = srcFix->universeAddress();
    for (quint32 i = srcAddr; i < srcAddr + srcFix->channels(); i++)
    {
        quint32 fxCheck = m_targetDoc->fixtureForAddress(i);
        if (fxCheck != Fixture::invalidId())
        {
            QMessageBox::warning(this,
                                 tr("Invalid operation"),
                                 tr("You are trying to clone a fixture on an address already in use. "
                                    "Please fix the target list first."));
            return;
        }
    }

    // create a copy of the fixture and add it to the target document
    /* Create the target fixture */
    Fixture* tgtFix = new Fixture(m_targetDoc);

    /* Add the first fixture without gap, at the given address */
    tgtFix->setAddress(srcFix->address());
    tgtFix->setUniverse(srcFix->universe());
    tgtFix->setName(srcFix->name());

    /* Set a fixture definition & mode if they were selected.
       Otherwise assign channels to a generic dimmer. */
    if (srcFix->fixtureDef()->manufacturer() == KXMLFixtureGeneric &&
        srcFix->fixtureDef()->model() == KXMLFixtureGeneric)
            tgtFix->setChannels(srcFix->channels());
    else
        tgtFix->setFixtureDefinition(srcFix->fixtureDef(), srcFix->fixtureMode());

    m_targetDoc->addFixture(tgtFix);

    // create the tree element and add it to the target tree
    QTreeWidgetItem *topItem = getUniverseItem(m_targetDoc, tgtFix->universe(), m_targetTree);
    quint32 baseAddr = tgtFix->address();
    QTreeWidgetItem *fItem = new QTreeWidgetItem(topItem);
    fItem->setText(KColumnName, tgtFix->name());
    fItem->setIcon(KColumnName, tgtFix->getIconFromType());
    fItem->setText(KColumnAddress, QString("%1 - %2").arg(baseAddr + 1).arg(baseAddr + tgtFix->channels()));
    fItem->setText(KColumnUniverse, QString::number(tgtFix->universe()));
    fItem->setText(KColumnID, QString::number(tgtFix->id()));

    for (quint32 c = 0; c < tgtFix->channels(); c++)
    {
        const QLCChannel* channel = tgtFix->channel(c);
        QTreeWidgetItem *item = new QTreeWidgetItem(fItem);
        item->setText(KColumnName, QString("%1:%2").arg(c + 1)
                      .arg(channel->name()));
        item->setIcon(KColumnName, channel->getIcon());
        item->setText(KColumnUniverse, QString::number(tgtFix->universe()));
        item->setText(KColumnID, QString::number(tgtFix->id()));
        item->setText(KColumnChIdx, QString::number(c));
    }

    m_targetTree->resizeColumnToContents(KColumnName);

    foreach (QTreeWidgetItem *it, m_targetTree->selectedItems())
        it->setSelected(false);
    fItem->setSelected(true);

    slotAddRemap();
}

void FixtureRemap::slotAddRemap()
{
    if (m_sourceTree->selectedItems().count() == 0 ||
        m_targetTree->selectedItems().count() == 0)
    {
        QMessageBox::warning(this,
                tr("Invalid selection"),
                tr("Please select a source and a target fixture or channel to perform this operation."));
        return;
    }

    connectFixtures(m_sourceTree->selectedItems().first(),
                    m_targetTree->selectedItems().first());

    remapWidget->setRemapList(m_remapList);
}

void FixtureRemap::connectFixtures(QTreeWidgetItem *sourceItem, QTreeWidgetItem *targetItem)
{
    if (sourceItem == NULL || targetItem == NULL)
        return;

    RemapInfo newRemap;
    newRemap.source = sourceItem;
    newRemap.target = targetItem;

    quint32 srcFxiID = newRemap.source->text(KColumnID).toUInt();
    Fixture *srcFxi = m_doc->fixture(srcFxiID);
    quint32 tgtFxiID = newRemap.target->text(KColumnID).toUInt();
    Fixture *tgtFxi = m_targetDoc->fixture(tgtFxiID);
    if (srcFxi == NULL || tgtFxi == NULL)
    {
        QMessageBox::warning(this,
                tr("Invalid selection"),
                tr("Please select a source and a target fixture or channel to perform this operation."));
        return;
    }

    bool srcFxiSelected = false;
    bool tgtFxiSelected = false;

    bool ok = false;
    int srcIdx = newRemap.source->text(KColumnChIdx).toInt(&ok);
    if (ok == false)
        srcFxiSelected = true;
    ok = false;
    int tgtIdx = newRemap.target->text(KColumnChIdx).toInt(&ok);
    if (ok == false)
        tgtFxiSelected = true;

    qDebug() << "Idx:" << srcIdx << ", src:" << srcFxiSelected << ", tgt:" << tgtFxiSelected;

    if ((srcFxiSelected == true && tgtFxiSelected == false) ||
        (srcFxiSelected == false && tgtFxiSelected == true))
    {
        QMessageBox::warning(this,
                             tr("Invalid selection"),
                             tr("To perform a fixture remap, please select fixtures on both lists."));
        return;
    }
    else if (srcFxiSelected == true && tgtFxiSelected == true)
    {
        // perform a full fixture remap
        const QLCFixtureDef *srcFxiDef = srcFxi->fixtureDef();
        const QLCFixtureDef *tgtFxiDef = tgtFxi->fixtureDef();
        const QLCFixtureMode *srcFxiMode = srcFxi->fixtureMode();
        const QLCFixtureMode *tgtFxiMode = tgtFxi->fixtureMode();
        bool oneToOneRemap = false;

        if (m_remapNamesCheck->isChecked())
        {
            tgtFxi->setName(srcFxi->name());
            newRemap.target->setText(KColumnName, srcFxi->name());
        }

        // 1-to-1 channel remapping is required for fixtures with
        // the same definition and mode
        if (srcFxiDef != NULL && tgtFxiDef != NULL &&
            srcFxiMode != NULL && tgtFxiMode != NULL)
        {
            if (srcFxiDef->name() == tgtFxiDef->name() &&
                srcFxiMode->name() == tgtFxiMode->name())
                    oneToOneRemap = true;
        }
        // 1-to-1 channel remapping is required for
        // generic dimmer packs
        else if (srcFxiDef == NULL && tgtFxiDef == NULL &&
                 srcFxiMode == NULL && tgtFxiMode == NULL)
                    oneToOneRemap = true;

        if (oneToOneRemap == true)
        {
            // copy forced LTP/HTP channels right away
            tgtFxi->setForcedHTPChannels(srcFxi->forcedHTPChannels());
            tgtFxi->setForcedLTPChannels(srcFxi->forcedLTPChannels());
        }

        for (quint32 s = 0; s < srcFxi->channels(); s++)
        {
            if (oneToOneRemap == true)
            {
                if (s < tgtFxi->channels())
                {
                    RemapInfo matchInfo;
                    matchInfo.source = newRemap.source->child(s);
                    matchInfo.target = newRemap.target->child(s);
                    m_remapList.append(matchInfo);

                    if (srcFxi->channelCanFade(s) == false)
                        tgtFxi->setChannelCanFade(s, false);

                    // copy channel modifiers
                    ChannelModifier *chMod = srcFxi->channelModifier(s);
                    if (chMod != NULL)
                        tgtFxi->setChannelModifier(s, chMod);
                }
            }
            else
            {
                const QLCChannel *srcCh = srcFxi->channel(s);

                for (quint32 t = 0; t < tgtFxi->channels(); t++)
                {
                    const QLCChannel *tgtCh = tgtFxi->channel(t);

                    if ((tgtCh->group() == srcCh->group()) &&
                        (tgtCh->controlByte() == srcCh->controlByte()))
                    {
                        if (tgtCh->group() == QLCChannel::Intensity &&
                            tgtCh->colour() != srcCh->colour())
                                continue;

                        RemapInfo matchInfo;
                        matchInfo.source = newRemap.source->child(s);
                        matchInfo.target = newRemap.target->child(t);
                        m_remapList.append(matchInfo);

                        if (srcFxi->channelCanFade(s) == false)
                            tgtFxi->setChannelCanFade(t, false);
                        break;
                    }
                }
            }
        }
    }
    else
    {
        // perform a single channel remap
        m_remapList.append(newRemap);
        if (srcFxi->channelCanFade(srcIdx) == false)
            tgtFxi->setChannelCanFade(tgtIdx, false);
    }
}

void FixtureRemap::slotRemoveRemap()
{
    if (m_sourceTree->selectedItems().count() == 0 ||
        m_targetTree->selectedItems().count() == 0)
    {
        QMessageBox::warning(this,
                             tr("Invalid selection"),
                             tr("Please select a source and a target fixture or channel to perform this operation."));
        return;
    }

    RemapInfo delRemap;
    delRemap.source = m_sourceTree->selectedItems().first();
    delRemap.target = m_targetTree->selectedItems().first();

    bool tgtFxiSelected = false;
    bool fxok = false, chok = false;
    quint32 fxid = delRemap.target->text(KColumnID).toUInt(&fxok);
    delRemap.target->text(KColumnChIdx).toInt(&chok);
    if (fxok == true && chok == false)
        tgtFxiSelected = true;

    for (int i = 0; i < m_remapList.count(); i++)
    {
        RemapInfo info = m_remapList.at(i);
        // full fixture remap delete
        if (tgtFxiSelected == true)
        {
            quint32 rmpFxID = info.target->text(KColumnID).toUInt();
            if (rmpFxID == fxid)
            {
                m_remapList.takeAt(i);
                i--;
            }
        }
        // single channel remap delete. Source and target must match
        else if (info.source == delRemap.source && info.target == delRemap.target)
        {
            m_remapList.takeAt(i);
            i--;
        }
    }
    remapWidget->setRemapList(m_remapList);
}

void FixtureRemap::slotUpdateConnections()
{
    remapWidget->update();
    m_sourceTree->resizeColumnToContents(KColumnName);
    m_targetTree->resizeColumnToContents(KColumnName);
}

void FixtureRemap::slotSourceSelectionChanged()
{
    if (m_sourceTree->selectedItems().count() > 0)
    {
        QTreeWidgetItem *item = m_sourceTree->selectedItems().first();
        bool fxOK = false, chOK = false;
        item->text(KColumnID).toUInt(&fxOK);
        item->text(KColumnChIdx).toInt(&chOK);
        if (fxOK == true && chOK == false)
            m_cloneButton->setEnabled(true);
        else
            m_cloneButton->setEnabled(false);
    }
    else
        m_cloneButton->setEnabled(false);
}

QList<SceneValue> FixtureRemap::remapSceneValues(QList<SceneValue> funcList,
                                    QList<SceneValue> &srcList,
                                    QList<SceneValue> &tgtList)
{
    QList <SceneValue> newValuesList;
    foreach (SceneValue val, funcList)
    {
        for (int v = 0; v < srcList.count(); v++)
        {
            if (val == srcList.at(v))
            {
                SceneValue tgtVal = tgtList.at(v);
                //qDebug() << "[Scene] Remapping" << val.fxi << val.channel << " to " << tgtVal.fxi << tgtVal.channel;
                newValuesList.append(SceneValue(tgtVal.fxi, tgtVal.channel, val.value));
            }
        }
    }
    std::sort(newValuesList.begin(), newValuesList.end());
    return newValuesList;
}

void FixtureRemap::accept()
{
    /* **********************************************************************
     * 1 - create a map of SceneValues from the fixtures channel associations
     * ********************************************************************** */
    QList<SceneValue> sourceList;
    QList<SceneValue> targetList;

    foreach (RemapInfo info, m_remapList)
    {
        quint32 srcFxiID = info.source->text(KColumnID).toUInt();
        quint32 srcChIdx = info.source->text(KColumnChIdx).toUInt();

        quint32 tgtFxiID = info.target->text(KColumnID).toUInt();
        quint32 tgtChIdx = info.target->text(KColumnChIdx).toUInt();

        sourceList.append(SceneValue(srcFxiID, srcChIdx));
        targetList.append(SceneValue(tgtFxiID, tgtChIdx));

        // qDebug() << "Remapping fx" << srcFxiID << "ch" << srcChIdx << "to fx" << tgtFxiID << "ch" << tgtChIdx;
    }

    /* **********************************************************************
     * 2 - Show a progress dialog, in case the operation takes a while
     * ********************************************************************** */
    QProgressDialog progress(tr("This might take a while..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    /* **********************************************************************
     * 3 - replace original project fixtures
     * ********************************************************************** */

    m_doc->replaceFixtures(m_targetDoc->fixtures());

    /* **********************************************************************
     * 4 - remap fixture groups and channel groups
     * ********************************************************************** */
    foreach (FixtureGroup *group, m_doc->fixtureGroups())
    {
        QMap<QLCPoint, GroupHead> grpHash = group->headsMap();
        group->reset();

        QMapIterator<QLCPoint, GroupHead> it(grpHash);
        while (it.hasNext())
        {
            it.next();

            QLCPoint pt(it.key());
            GroupHead head(it.value());

            if (head.isValid() == false)
                continue;

            for (int i = 0; i < sourceList.count(); i++)
            {
                if (sourceList.at(i).fxi == head.fxi)
                {
                    head.fxi = targetList.at(i).fxi;
                    group->resignHead(pt);
                    group->assignHead(pt, head);
                    break;
                }
            }
        }
    }

    foreach (ChannelsGroup *grp, m_doc->channelsGroups())
    {
        QList<SceneValue> grpChannels = grp->getChannels();
        // this is crucial: here all the "unmapped" channels will be lost forever !
        grp->resetChannels();
        QList <SceneValue> newList = remapSceneValues(grpChannels, sourceList, targetList);
        foreach (SceneValue val, newList)
            grp->addChannel(val.fxi, val.channel);
    }

    /* **********************************************************************
     * 5 - scan project functions and perform remapping
     * ********************************************************************** */
    int funcNum = m_doc->functions().count();
    int f = 0;
    foreach (Function *func, m_doc->functions())
    {
        switch (func->type())
        {
            case Function::SceneType:
            {
                Scene *s = qobject_cast<Scene*>(func);
                qDebug() << "Analyzing Scene #" << s->id();
                QList <SceneValue> newList = remapSceneValues(s->values(), sourceList, targetList);
                // this is crucial: here all the "unmapped" channels will be lost forever !
                s->clear();

                for (int i = 0; i < newList.count(); i++)
                {
                    s->addFixture(newList.at(i).fxi);
                    s->setValue(newList.at(i));
                }
            }
            break;
            case Function::SequenceType:
            {
                Sequence *s = qobject_cast<Sequence*>(func);
                for (int idx = 0; idx < s->stepsCount(); idx++)
                {
                    ChaserStep *cs = s->stepAt(idx);
                    QList <SceneValue> newList = remapSceneValues(cs->values, sourceList, targetList);
                    //qDebug() << "Step" << idx << "remapped" << cs.values.count() << "to" << newList.count();
                    // this is crucial: here all the "unmapped" channels will be lost forever !
                    cs->values.clear();
                    cs->values = newList;
                    //s->replaceStep(cs, idx);
                }
            }
            break;
            case Function::EFXType:
            {
                EFX *e = qobject_cast<EFX*>(func);
                // make a copy of this EFX fixtures list
                QList <EFXFixture*> fixListCopy;
                foreach (EFXFixture *efxFix, e->fixtures())
                {
                    EFXFixture* ef = new EFXFixture(e);
                    ef->copyFrom(efxFix);
                    fixListCopy.append(ef);
                }
                // this is crucial: here all the "unmapped" fixtures will be lost forever !
                e->removeAllFixtures();
                QList<quint32>remappedFixtures;

                foreach (EFXFixture *efxFix, fixListCopy)
                {
                    quint32 fxID = efxFix->head().fxi;
                    for (int i = 0; i < sourceList.count(); i++)
                    {
                        SceneValue srcVal = sourceList.at(i);
                        SceneValue tgtVal = targetList.at(i);
                        // check for fixture ID match. EFX remapping must be performed
                        // just once for each target fixture
                        if (srcVal.fxi == fxID && remappedFixtures.contains(tgtVal.fxi) == false)
                        {
                            Fixture *docFix = m_doc->fixture(tgtVal.fxi);
                            quint32 fxCh = tgtVal.channel;
                            const QLCChannel *chan = docFix->channel(fxCh);
                            if (chan->group() == QLCChannel::Pan ||
                                chan->group() == QLCChannel::Tilt)
                            {
                                EFXFixture* ef = new EFXFixture(e);
                                ef->copyFrom(efxFix);
                                ef->setHead(GroupHead(tgtVal.fxi, 0)); // TODO!!! head!!!
                                if (e->addFixture(ef) == false)
                                    delete ef;
                                qDebug() << "EFX remap" << srcVal.fxi << "to" << tgtVal.fxi;
                                remappedFixtures.append(tgtVal.fxi);
                            }
                        }
                    }
                }
                fixListCopy.clear();
            }
            break;
            default:
            break;
        }
        if (progress.wasCanceled())
            break;
        f++;
        progress.setValue((f * 100) / funcNum);
        QApplication::processEvents();
    }

    /* **********************************************************************
     * 6 - remap Virtual Console widgets
     * ********************************************************************** */
    VCFrame* contents = VirtualConsole::instance()->contents();
    QList<VCWidget *> widgetsList = contents->findChildren<VCWidget*>();

    foreach (VCWidget *widget, widgetsList)
    {
        switch (widget->type())
        {
            case VCWidget::SliderWidget:
            {
                VCSlider *slider = qobject_cast<VCSlider*>(widget);
                if (slider->sliderMode() == VCSlider::Level)
                {
                    qDebug() << "Remapping slider:" << slider->caption();
                    QList <SceneValue> newChannels;

                    foreach (VCSlider::LevelChannel chan, slider->levelChannels())
                    {
                        for (int v = 0; v < sourceList.count(); v++)
                        {
                            SceneValue val = sourceList.at(v);
                            if (val.fxi == chan.fixture && val.channel == chan.channel)
                            {
                                qDebug() << "Matching channel:" << chan.fixture << chan.channel << "to target:" << targetList.at(v).fxi << targetList.at(v).channel;
                                newChannels.append(SceneValue(targetList.at(v).fxi, targetList.at(v).channel));
                            }
                        }
                    }
                    // this is crucial: here all the "unmapped" channels will be lost forever !
                    slider->clearLevelChannels();
                    foreach (SceneValue rmpChan, newChannels)
                        slider->addLevelChannel(rmpChan.fxi, rmpChan.channel);
                }
            }
            break;
            case VCWidget::AudioTriggersWidget:
            {
                VCAudioTriggers *triggers = qobject_cast<VCAudioTriggers*>(widget);
                foreach (AudioBar *bar, triggers->getAudioBars())
                {
                    if (bar->m_type == AudioBar::DMXBar)
                    {
                        QList <SceneValue> newList = remapSceneValues(bar->m_dmxChannels, sourceList, targetList);
                        // this is crucial: here all the "unmapped" channels will be lost forever !
                        bar->attachDmxChannels(m_doc, newList);
                    }
                }
            }
            break;
            case VCWidget::XYPadWidget:
            {
                VCXYPad *xypad = qobject_cast<VCXYPad*>(widget);
                QList<VCXYPadFixture> copyFixtures;
                foreach (VCXYPadFixture fix, xypad->fixtures())
                {
                    quint32 srxFxID = fix.head().fxi; // TODO: heads !!
                    for (int i = 0; i < sourceList.count(); i++)
                    {
                        SceneValue val = sourceList.at(i);
                        if (val.fxi == srxFxID)
                        {
                            SceneValue tgtVal = targetList.at(i);
                            Fixture *docFix = m_doc->fixture(tgtVal.fxi);
                            quint32 fxCh = tgtVal.channel;
                            const QLCChannel *chan = docFix->channel(fxCh);
                            if (chan->group() == QLCChannel::Pan ||
                                chan->group() == QLCChannel::Tilt)
                            {
                                VCXYPadFixture tgtFix(m_doc);
                                GroupHead head(tgtVal.fxi, 0);
                                tgtFix.setHead(head);
                                copyFixtures.append(tgtFix);
                            }
                        }
                    }
                }
                // this is crucial: here all the "unmapped" fixtures will be lost forever !
                xypad->clearFixtures();
                foreach (VCXYPadFixture fix, copyFixtures)
                    xypad->appendFixture(fix);
            }
            break;
            default:
            break;
        }
    }

    /* **********************************************************************
     * 7 - remap 2D monitor properties, if defined
     * ********************************************************************** */
    MonitorProperties *props = m_doc->monitorProperties();
    if (props != NULL)
    {
        QMap <quint32, FixturePreviewItem> remappedFixtureItems;

        foreach (quint32 fxID, props->fixtureItemsID())
        {
            for (int v = 0; v < sourceList.count(); v++)
            {
                if (sourceList.at(v).fxi == fxID)
                {
                    FixturePreviewItem rmpProp = props->fixtureProperties(fxID);
                    remappedFixtureItems[targetList.at(v).fxi] = rmpProp;
                    break;
                }
            }

            props->removeFixture(fxID);
        }

        QMapIterator <quint32, FixturePreviewItem> it(remappedFixtureItems);
        while (it.hasNext())
        {
            it.next();
            props->setFixtureProperties(it.key(), it.value());
        }
    }

    /* **********************************************************************
     * 8 - save the remapped project into a new file
     * ********************************************************************** */
    App *mainApp = (App *)m_doc->parent();
    if (m_targetProjectLabel->text().endsWith(".qxw") == false)
        m_targetProjectLabel->setText(m_targetProjectLabel->text() + ".qxw");
    mainApp->setFileName(m_targetProjectLabel->text());
    mainApp->slotFileSave();

    progress.hide();

    /* Close dialog */
    QDialog::accept();
}
