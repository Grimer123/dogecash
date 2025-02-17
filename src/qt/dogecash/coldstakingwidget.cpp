// Copyright (c) 2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/dogecash/coldstakingwidget.h"
#include "qt/dogecash/forms/ui_coldstakingwidget.h"
#include "qt/dogecash/qtutils.h"
#include "amount.h"
#include "guiutil.h"
#include "qt/dogecash/requestdialog.h"
#include "qt/dogecash/tooltipmenu.h"
#include "qt/dogecash/furlistrow.h"
#include "qt/dogecash/sendconfirmdialog.h"
#include "qt/dogecash/addnewcontactdialog.h"
#include "qt/dogecash/guitransactionsutils.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "coincontroldialog.h"
#include "coincontrol.h"
#include "qt/dogecash/csrow.h"

#define DECORATION_SIZE 70
#define NUM_ITEMS 3
#define LOAD_MIN_TIME_INTERVAL 15
#define REQUEST_LOAD_TASK 1


class CSDelegationHolder : public FurListRow<QWidget*>
{
public:
    CSDelegationHolder();

    explicit CSDelegationHolder(bool _isLightTheme) : FurListRow(), isLightTheme(_isLightTheme){}

    CSRow* createHolder(int pos) override{
        if (!cachedRow) cachedRow = new CSRow();
        return cachedRow;
    }

    void init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const override{
        CSRow *row = static_cast<CSRow*>(holder);
        row->updateState(isLightTheme, isHovered, isSelected);

        QString address = index.data(Qt::DisplayRole).toString();
        QString label = index.sibling(index.row(), ColdStakingModel::OWNER_ADDRESS_LABEL).data(Qt::DisplayRole).toString();
        if (label.isEmpty()) {
            label = "Address with no label";
        }
        bool isWhitelisted = index.sibling(index.row(), ColdStakingModel::IS_WHITELISTED).data(Qt::DisplayRole).toBool();
        QString amountStr = index.sibling(index.row(), ColdStakingModel::TOTAL_STACKEABLE_AMOUNT_STR).data(Qt::DisplayRole).toString();
        bool isReceivedDelegation = index.sibling(index.row(), ColdStakingModel::IS_RECEIVED_DELEGATION).data(Qt::DisplayRole).toBool();
        row->updateView(address, label, isWhitelisted, isReceivedDelegation, amountStr);
        row->showMenuButton(isReceivedDelegation);
    }

    QColor rectColor(bool isHovered, bool isSelected) override{
        return getRowColor(isLightTheme, isHovered, isSelected);
    }

    ~CSDelegationHolder() override {
        if (cachedRow)
            delete cachedRow;
    }

    bool isLightTheme;
private:
    CSRow *cachedRow = nullptr;
};

ColdStakingWidget::ColdStakingWidget(DogeCashGUI* parent) :
    PWidget(parent),
    ui(new Ui::ColdStakingWidget)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());

    /* Containers */
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0,20,0,0);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(0,10,0,20);

    /* Light Font */
    QFont fontLight;
    fontLight.setWeight(QFont::Light);

    /* Title */
    ui->labelTitle->setText(tr("Cold Staking"));
    setCssTitleScreen(ui->labelTitle);
    ui->labelTitle->setFont(fontLight);

    /* Button Group */
    ui->pushLeft->setText(tr("Staker"));
    ui->pushRight->setText(tr("Delegation"));
    setCssProperty(ui->pushLeft, "btn-check-left");
    setCssProperty(ui->pushRight, "btn-check-right");

    /* Subtitle */
    ui->labelSubtitle1->setText(tr("You can delegate your DOGECs and let a hot node (24/7 online node)\nstake in your behalf, keeping the keys in a secure place offline."));
    setCssSubtitleScreen(ui->labelSubtitle1);
    spacerDiv = new QSpacerItem(40, 20, QSizePolicy::Maximum, QSizePolicy::Expanding);

    setCssProperty(ui->labelSubtitleDescription, "text-title");
    ui->lineEditOwnerAddress->setPlaceholderText(tr("Add owner address"));
    btnOwnerContact = ui->lineEditOwnerAddress->addAction(QIcon("://ic-contact-arrow-down"), QLineEdit::TrailingPosition);
    setCssProperty(ui->lineEditOwnerAddress, "edit-primary-multi-book");
    ui->lineEditOwnerAddress->setAttribute(Qt::WA_MacShowFocusRect, 0);
    setShadow(ui->lineEditOwnerAddress);

    ui->labelSubtitle2->setText(tr("Delegate or Accept DOGEC delegation"));
    setCssSubtitleScreen(ui->labelSubtitle2);
    ui->labelSubtitle2->setContentsMargins(0,2,0,0);

    ui->pushButtonSend->setText(tr("Delegate"));
    ui->pushButtonClear->setText(tr("Clear All"));
    setCssBtnPrimary(ui->pushButtonSend);
    setCssBtnSecondary(ui->pushButtonClear);

    connect(ui->pushButtonClear, SIGNAL(clicked()), this, SLOT(clearAll()));

    ui->labelEditTitle->setText(tr("Add the staking address"));
    setCssProperty(ui->labelEditTitle, "text-title");
    sendMultiRow = new SendMultiRow(this);
    sendMultiRow->setOnlyStakingAddressAccepted(true);
    ((QVBoxLayout*)ui->containerSend->layout())->insertWidget(1, sendMultiRow);
    connect(sendMultiRow, &SendMultiRow::onContactsClicked, [this](){ onContactsClicked(false); });

    // List
    ui->labelListHistory->setText(tr("Delegated balance history"));
    setCssProperty(ui->labelStakingTotal, "text-title-right");
    setCssProperty(ui->labelListHistory, "text-title");
    setCssProperty(ui->pushImgEmpty, "img-empty-transactions");
    ui->labelEmpty->setText(tr("No delegations yet"));
    setCssProperty(ui->labelEmpty, "text-empty");

    ui->btnCoinControl->setTitleClassAndText("btn-title-grey", "Coin Control");
    ui->btnCoinControl->setSubTitleClassAndText("text-subtitle", "Select DOGEC outputs to delegate.");

    ui->btnColdStaking->setTitleClassAndText("btn-title-grey", "Create Cold Stake Address");
    ui->btnColdStaking->setSubTitleClassAndText("text-subtitle", "Creates an address to receive coin\ndelegations and be able to stake them.");
    ui->btnColdStaking->layout()->setMargin(0);

    connect(ui->btnCoinControl, SIGNAL(clicked()), this, SLOT(onCoinControlClicked()));
    connect(ui->btnColdStaking, SIGNAL(clicked()), this, SLOT(onColdStakeClicked()));

    onDelegateSelected(true);
    ui->pushRight->setChecked(true);
    connect(ui->pushLeft, &QPushButton::clicked, [this](){onDelegateSelected(false);});
    connect(ui->pushRight,  &QPushButton::clicked, [this](){onDelegateSelected(true);});

    // List
    setCssProperty(ui->listView, "container");
    txHolder = new CSDelegationHolder(isLightTheme());
    delegate = new FurAbstractListItemDelegate(
                DECORATION_SIZE,
                txHolder,
                this
    );

    addressHolder = new AddressHolder(isLightTheme());
    addressDelegate = new FurAbstractListItemDelegate(
            DECORATION_SIZE,
            addressHolder,
            this
    );

    ui->listView->setItemDelegate(delegate);
    ui->listView->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listView->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listView->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listView->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->btnMyStakingAddresses->setChecked(true);
    ui->listViewStakingAddress->setVisible(false);

    ui->btnMyStakingAddresses->setTitleClassAndText("btn-title-grey", "My staking addresses");
    ui->btnMyStakingAddresses->setSubTitleClassAndText("text-subtitle", "List your own staking addresses.");
    ui->btnMyStakingAddresses->layout()->setMargin(0);
    ui->btnMyStakingAddresses->setRightIconClass("ic-arrow");

    // List Addresses
    setCssProperty(ui->listViewStakingAddress, "container");
    ui->listViewStakingAddress->setItemDelegate(addressDelegate);
    ui->listViewStakingAddress->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listViewStakingAddress->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listViewStakingAddress->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listViewStakingAddress->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(ui->pushButtonSend, &QPushButton::clicked, this, &ColdStakingWidget::onSendClicked);
    connect(btnOwnerContact, &QAction::triggered, [this](){ onContactsClicked(true); });
    connect(ui->listView, SIGNAL(clicked(QModelIndex)), this, SLOT(handleAddressClicked(QModelIndex)));
    connect(ui->listViewStakingAddress, SIGNAL(clicked(QModelIndex)), this, SLOT(handleMyColdAddressClicked(QModelIndex)));
    connect(ui->btnMyStakingAddresses, SIGNAL(clicked()), this, SLOT(onMyStakingAddressesClicked()));
}

void ColdStakingWidget::loadWalletModel(){
    if(walletModel) {
        sendMultiRow->setWalletModel(walletModel);
        txModel = walletModel->getTransactionTableModel();
        csModel = new ColdStakingModel(walletModel, txModel, walletModel->getAddressTableModel(), this);
        ui->listView->setModel(csModel);
        ui->listView->setModelColumn(ColdStakingModel::OWNER_ADDRESS);

        if (csModel->rowCount() > 0) {
            CAmount coldStaking = walletModel->getColdStakedBalance();
            ui->labelStakingTotal->setText(tr("Total Staking: %1").arg(
                    (coldStaking == 0) ? "0.00 DOGEC" : GUIUtil::formatBalance(coldStaking, nDisplayUnit))
            );
            ui->labelStakingTotal->setVisible(true);
        } else {
            ui->labelStakingTotal->setVisible(false);
        }

        addressTableModel = walletModel->getAddressTableModel();
        addressesFilter = new AddressFilterProxyModel(AddressTableModel::ColdStaking, this);
        addressesFilter->setSourceModel(addressTableModel);
        ui->listViewStakingAddress->setModel(addressesFilter);
        ui->listViewStakingAddress->setModelColumn(AddressTableModel::Address);

        connect(txModel, &TransactionTableModel::txArrived, this, &ColdStakingWidget::onTxArrived);

        updateDisplayUnit();

        ui->containerHistoryLabel->setVisible(false);
        ui->emptyContainer->setVisible(false);
        ui->listView->setVisible(false);
    }

}

void ColdStakingWidget::onTxArrived(const QString& hash, const bool& isCoinStake, const bool& isCSAnyType) {
    if (isCSAnyType) {
        tryRefreshDelegations();
    }
}

void ColdStakingWidget::walletSynced(bool sync) {
    tryRefreshDelegations();
}

void ColdStakingWidget::tryRefreshDelegations() {
    // Check for min update time to not reload the UI so often if the node is syncing.
    int64_t now = GetTime();
    if (lastRefreshTime + LOAD_MIN_TIME_INTERVAL < now) {
        lastRefreshTime = now;
        refreshDelegations();
    }
}

bool ColdStakingWidget::refreshDelegations(){
    if (isLoading) return false;
    isLoading = true;
    return execute(REQUEST_LOAD_TASK);
}

void ColdStakingWidget::onDelegationsRefreshed() {
    isLoading = false;
    if (ui->pushLeft->isChecked())
        showList(csModel->rowCount() > 0);
}

void ColdStakingWidget::run(int type) {
    if (type == REQUEST_LOAD_TASK) {
        csModel->updateCSList();
        QMetaObject::invokeMethod(this, "onDelegationsRefreshed", Qt::QueuedConnection);
    }
}
void ColdStakingWidget::onError(QString error, int type) {
    isLoading = false;
    inform(tr("Error loading delegations: %1").arg(error));
}

void ColdStakingWidget::onContactsClicked(bool ownerAdd) {
    isContactOwnerSelected = ownerAdd;
    onContactsClicked();
}

void ColdStakingWidget::onContactsClicked(){

    if(menu && menu->isVisible()){
        menu->hide();
    }

    int contactsSize = isContactOwnerSelected ? walletModel->getAddressTableModel()->sizeSend() : walletModel->getAddressTableModel()->sizeColdSend();
    if(contactsSize == 0) {
        inform(tr("No contacts available, you can go to the contacts screen and add some there!"));
        return;
    }

    int height;
    int width;
    QPoint pos;

    menuContacts->setWalletModel(walletModel, isContactOwnerSelected ? AddressTableModel::Receive : AddressTableModel::ColdStakingSend);

    pos.setX(pos.x() + 40);
    height = (contactsSize <= 2) ? height * ( 2 * (contactsSize + 1 )) : height * 4;

    if(!menuContacts){
        menuContacts = new ContactsDropdown(
                width,
                height,
                this
        );
        connect(menuContacts, &ContactsDropdown::contactSelected, [this](QString address, QString label){
            if (isContactOwnerSelected) {
                ui->lineEditOwnerAddress->setText(address);
            } else {
                sendMultiRow->setLabel(label);
                sendMultiRow->setAddress(address);
            }
        });
    }

    if(menuContacts->isVisible()){
        menuContacts->hide();
        return;
    }

    if (isContactOwnerSelected) {
        menuContacts->setWalletModel(walletModel, AddressTableModel::Receive);
    } else {
        menuContacts->setWalletModel(walletModel, AddressTableModel::Delegators);
    }

    menuContacts->resizeList(width, height);
    menuContacts->setStyleSheet(styleSheet());
    menuContacts->adjustSize();
    menuContacts->move(pos);
    menuContacts->show();
}

void ColdStakingWidget::onDelegateSelected(bool delegate){
    if (menu && menu->isVisible()) {
        menu->hide();
    }

    if (menuAddresses && menuAddresses->isVisible()) {
        menuAddresses->hide();
    }

    if(delegate){
        ui->btnCoinControl->setVisible(true);
        ui->containerSend->setVisible(true);
        ui->containerBtn->setVisible(true);
        ui->emptyContainer->setVisible(false);
        ui->listView->setVisible(false);
        ui->containerHistoryLabel->setVisible(false);
        ui->btnColdStaking->setVisible(false);
        ui->btnMyStakingAddresses->setVisible(false);
        ui->listViewStakingAddress->setVisible(false);
        if (ui->rightContainer->count() == 2)
            ui->rightContainer->addItem(spacerDiv);
    }else{
        ui->btnCoinControl->setVisible(false);
        ui->containerSend->setVisible(false);
        ui->containerBtn->setVisible(false);
        ui->btnColdStaking->setVisible(true);
        showList(csModel->rowCount() > 0);

        ui->btnMyStakingAddresses->setVisible(true);
        ui->listViewStakingAddress->setVisible(false);
    }
}

void ColdStakingWidget::updateDisplayUnit() {
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        ui->listView->update();
    }
}

void ColdStakingWidget::showList(bool show){
    ui->emptyContainer->setVisible(!show);
    ui->listView->setVisible(show);
    ui->containerHistoryLabel->setVisible(show);
}

void ColdStakingWidget::onSendClicked(){
    if (!walletModel || !walletModel->getOptionsModel() || !verifyWalletUnlocked())
        return;

    if (!walletModel->isColdStakingNetworkelyEnabled()) {
        inform(tr("Cold staking is networkely disabled"));
        return;
    }

    if (!sendMultiRow->validate()) {
        inform(tr("Invalid entry"));
        return;
    }

    SendCoinsRecipient dest = sendMultiRow->getValue();
    dest.isP2CS = true;

    // Amount must be < 10 DOGEC, check chainparams minColdStakingAmount
    if (dest.amount < (COIN * 10)) {
        inform(tr("Invalid entry, minimum delegable amount is 10 DOGEC"));
        return;
    }

    QString inputOwner = ui->lineEditOwnerAddress->text();
    bool isOwnerEmpty = inputOwner.isEmpty();
    if (!isOwnerEmpty && !walletModel->validateAddress(inputOwner)) {
        inform(tr("Owner address invalid"));
        return;
    }


    bool isStakingAddressFromThisWallet = walletModel->isMine(dest.address);
    bool isOwnerAddressFromThisWallet = isOwnerEmpty;

    if (!isOwnerAddressFromThisWallet) {
        isOwnerAddressFromThisWallet = walletModel->isMine(inputOwner);
    }

    // Don't try to delegate the balance if both addresses are from this wallet
    if (isStakingAddressFromThisWallet && isOwnerAddressFromThisWallet) {
        inform(tr("Staking address corresponds to this wallet, change it to an external node"));
        return;
    }

    dest.ownerAddress = inputOwner;
    QList<SendCoinsRecipient> recipients;
    recipients.append(dest);

    // Prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus = walletModel->prepareTransaction(currentTransaction, CoinControlDialog::coinControl);

    // process prepareStatus and on error generate message shown to user
    GuiTransactionsUtils::ProcessSendCoinsReturn(
            this,
            prepareStatus,
            walletModel,
            BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(),
                                         currentTransaction.getTransactionFee()),
            true
    );

    if (prepareStatus.status != WalletModel::OK) {
        inform(tr("Cannot create transaction."));
        return;
    }

    showHideOp(true);
    TxDetailDialog* dialog = new TxDetailDialog(window);
    dialog->setDisplayUnit(walletModel->getOptionsModel()->getDisplayUnit());
    dialog->setData(walletModel, currentTransaction);
    dialog->adjustSize();
    openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);

    if(dialog->isConfirm()){
        // now send the prepared transaction
        WalletModel::SendCoinsReturn sendStatus = dialog->getStatus();
        // process sendStatus and on error generate message shown to user
        GuiTransactionsUtils::ProcessSendCoinsReturn(
                this,
                sendStatus,
                walletModel
        );

        if (sendStatus.status == WalletModel::OK) {
            clearAll();
            inform(tr("Coins delegated"));
        }
    }

    dialog->deleteLater();
}

void ColdStakingWidget::clearAll() {
    if (sendMultiRow) sendMultiRow->clear();
    ui->lineEditOwnerAddress->clear();
    if (CoinControlDialog::coinControl) {
        CoinControlDialog::coinControl->SetNull();
        ui->btnCoinControl->setActive(false);
    }
}

void ColdStakingWidget::onCoinControlClicked(){
    if(ui->pushRight->isChecked()) {
        if (walletModel->getBalance() > 0) {
            if (!coinControlDialog) {
                coinControlDialog = new CoinControlDialog();
                coinControlDialog->setModel(walletModel);
            } else {
                coinControlDialog->updateView();
            }
            coinControlDialog->exec();
            ui->btnCoinControl->setActive(CoinControlDialog::coinControl->HasSelected());
        } else {
            inform(tr("You don't have any DOGEC to select."));
        }
    }
}

void ColdStakingWidget::onColdStakeClicked() {
    showAddressGenerationDialog(false);
}

void ColdStakingWidget::showAddressGenerationDialog(bool isPaymentRequest) {
    if(walletModel && !isShowingDialog) {
        if (!verifyWalletUnlocked()) return;
        isShowingDialog = true;
        showHideOp(true);
        RequestDialog *dialog = new RequestDialog(window);
        dialog->setWalletModel(walletModel);
        dialog->setPaymentRequest(isPaymentRequest);
        openDialogWithOpaqueBackgroundY(dialog, window, 3.5, 12);
        if (dialog->res == 1){
            inform(tr("URI copied to clipboard"));
        } else if (dialog->res == 2){
            inform(tr("Address copied to clipboard"));
        }
        dialog->deleteLater();
        isShowingDialog = false;
    }
}

void ColdStakingWidget::handleMyColdAddressClicked(const QModelIndex &_index) {

    ui->listViewStakingAddress->setCurrentIndex(_index);

    QRect rect = ui->listViewStakingAddress->visualRect(_index);
    QPoint pos = rect.topRight();
    pos.setX( parentWidget()->rect().right() - (DECORATION_SIZE * 1.5) );
    pos.setY(pos.y() + (DECORATION_SIZE * 2.5));

    QModelIndex rIndex = addressesFilter->mapToSource(_index);

    if(!menuAddresses){
        menuAddresses = new TooltipMenu(window, this);
        menuAddresses->setEditBtnText(tr("Copy"));
        menuAddresses->setDeleteBtnText(tr("Edit"));
        menuAddresses->setCopyBtnVisible(false);
        menuAddresses->adjustSize();
        connect(menuAddresses, &TooltipMenu::message, this, &AddressesWidget::message);
        connect(menuAddresses, SIGNAL(onEditClicked()), this, SLOT(onAddressCopyClicked()));
        connect(menuAddresses, SIGNAL(onDeleteClicked()), this, SLOT(onAddressEditClicked()));
    }else {
        menuAddresses->hide();
    }

    this->addressIndex = rIndex;

    menuAddresses->move(pos);
    menuAddresses->show();
}

void ColdStakingWidget::handleAddressClicked(const QModelIndex &rIndex) {

    bool isReceivedDelegation = rIndex.sibling(rIndex.row(), ColdStakingModel::IS_RECEIVED_DELEGATION).data(Qt::DisplayRole).toBool();

    ui->listView->setCurrentIndex(rIndex);
    QRect rect = ui->listView->visualRect(rIndex);
    QPoint pos = rect.topRight();
    pos.setX(pos.x() - (DECORATION_SIZE * 2));
    pos.setY(pos.y() + (DECORATION_SIZE * 2));

    if(!this->menu){
        this->menu = new TooltipMenu(window, this);
        this->menu->setEditBtnText(tr("Stake"));
        this->menu->setDeleteBtnText(tr("Blacklist"));
        this->menu->setCopyBtnText(tr("Edit Label"));
        this->menu->setLastBtnText(tr("Copy owner\naddress"), 40);
        this->menu->setLastBtnVisible(true);
        this->menu->setMinimumHeight(157);
        this->menu->setFixedHeight(157);
        this->menu->setMinimumWidth(125);
        connect(this->menu, &TooltipMenu::message, this, &AddressesWidget::message);
        connect(this->menu, SIGNAL(onEditClicked()), this, SLOT(onEditClicked()));
        connect(this->menu, SIGNAL(onDeleteClicked()), this, SLOT(onDeleteClicked()));
        connect(this->menu, SIGNAL(onCopyClicked()), this, SLOT(onLabelClicked()));
        connect(this->menu, SIGNAL(onLastClicked()), this, SLOT(onCopyOwnerClicked()));
    }else {
        this->menu->hide();
    }

    this->index = rIndex;

    if (isReceivedDelegation) {
        bool isWhitelisted = rIndex.sibling(rIndex.row(), ColdStakingModel::IS_WHITELISTED).data(
                Qt::DisplayRole).toBool();
        this->menu->setDeleteBtnVisible(isWhitelisted);
        this->menu->setEditBtnVisible(!isWhitelisted);
        this->menu->setCopyBtnVisible(true);
        this->menu->setMinimumHeight(157);
    } else {
        // owner side
        this->menu->setDeleteBtnVisible(false);
        this->menu->setEditBtnVisible(false);
        this->menu->setCopyBtnVisible(false);
        this->menu->setMinimumHeight(60);
    }

    this->menu->adjustSize();

    menu->move(pos);
    menu->show();
}

void ColdStakingWidget::onAddressCopyClicked() {
    GUIUtil::setClipboard(addressIndex.data(Qt::DisplayRole).toString());
    inform(tr("Address copied"));
}
void ColdStakingWidget::onAddressEditClicked() {
    onLabelClicked(
            tr("Edit Cold Address Label"),
            addressIndex,
            false
    );
}

void ColdStakingWidget::onEditClicked() {
    // whitelist address
    if (!csModel->whitelist(index)) {
        inform(tr("Whitelist failed, please check the logs"));
        return;
    }
    QString label = index.sibling(index.row(), ColdStakingModel::OWNER_ADDRESS_LABEL).data(Qt::DisplayRole).toString();
    if (label.isEmpty()) {
        label = index.data(Qt::DisplayRole).toString();
    }
    inform(label + tr(" staking!"));
}

void ColdStakingWidget::onDeleteClicked() {
    // blacklist address
    if (!csModel->blacklist(index)) {
        inform(tr("Blacklist failed, please check the logs"));
        return;
    }
    QString label = index.sibling(index.row(), ColdStakingModel::OWNER_ADDRESS_LABEL).data(Qt::DisplayRole).toString();
    if (label.isEmpty()) {
        label = index.data(Qt::DisplayRole).toString();
    }
    inform(label + tr(" blacklisted from staking"));
}

void ColdStakingWidget::onCopyClicked() {
    // show address info
}

void ColdStakingWidget::onCopyOwnerClicked() {
    QString owner = index.data(Qt::DisplayRole).toString();
    GUIUtil::setClipboard(owner);
    inform(tr("Owner address copied"));
}

void ColdStakingWidget::onLabelClicked(){
    onLabelClicked(
            tr("Edit Owner Address Label"),
            index,
            false
    );
}

void ColdStakingWidget::onLabelClicked(QString dialogTitle, const QModelIndex &index, const bool& isMyColdStakingAddresses) {
    if(walletModel && !isShowingDialog) {
        isShowingDialog = true;
        showHideOp(true);
        AddNewContactDialog *dialog = new AddNewContactDialog(window);
        dialog->setTexts(dialogTitle);
        QString qAddress = index.data(Qt::DisplayRole).toString();
        dialog->setData(qAddress, walletModel->getAddressTableModel()->labelForAddress(qAddress));
        if (openDialogWithOpaqueBackgroundY(dialog, window, 3.5, 6)) {
            QString label = dialog->getLabel();
            std::string stdString = qAddress.toStdString();
            std::string purpose = walletModel->getAddressTableModel()->purposeForAddress(stdString);
            const CBitcoinAddress address = CBitcoinAddress(stdString.data());
            if (!label.isEmpty() && walletModel->updateAddressBookLabels(
                    address.Get(),
                    label.toUtf8().constData(),
                    purpose
            )) {
                if (isMyColdStakingAddresses) {
                    addressTableModel->notifyChange(index);
                } else
                    csModel->updateCSList();
                inform(tr("Address label saved"));
            } else {
                inform(tr("Error storing address label"));
            }
        }
        isShowingDialog = false;
    }
}

void ColdStakingWidget::onMyStakingAddressesClicked(){
    bool isVisible = ui->listViewStakingAddress->isVisible();
    if(!isVisible){
        ui->btnMyStakingAddresses->setRightIconClass("btn-dropdown", true);
        ui->listViewStakingAddress->setVisible(true);
        ui->rightContainer->removeItem(spacerDiv);
        ui->listViewStakingAddress->update();
    }else{
        ui->btnMyStakingAddresses->setRightIconClass("ic-arrow", true);
        ui->rightContainer->addItem(spacerDiv);
        ui->listViewStakingAddress->setVisible(false);
    }
}

void ColdStakingWidget::changeTheme(bool isLightTheme, QString& theme){
    static_cast<CSDelegationHolder*>(delegate->getRowFactory())->isLightTheme = isLightTheme;
    static_cast<AddressHolder*>(addressDelegate->getRowFactory())->isLightTheme = isLightTheme;
    ui->listView->update();
}

ColdStakingWidget::~ColdStakingWidget(){
    if (sendMultiRow)
        delete sendMultiRow;
    if (txHolder)
        delete txHolder;
    if (csModel)
        delete csModel;
    if (addressHolder)
        delete addressHolder;

    ui->rightContainer->removeItem(spacerDiv);
    delete spacerDiv;
    delete ui;
}