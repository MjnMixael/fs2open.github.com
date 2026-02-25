#include "stdafx.h"
#include "FRED.h"
#include "supportrearmdlg.h"

#include "mission/missionparse.h"
#include "weapon/weapon.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CSupportRearmDlg::CSupportRearmDlg(CWnd* pParent) : CDialog(CSupportRearmDlg::IDD, pParent) {
	m_disallow_support_ships = FALSE;
	m_limit_rearm_to_pool = FALSE;
	m_support_repairs_hull = FALSE;
	m_disallow_support_rearm = FALSE;
	m_allow_weapon_precedence = FALSE;
	m_rearm_pool_from_loadout = FALSE;
	m_max_hull_repair_val = 0.0f;
	m_max_subsys_repair_val = 100.0f;
	m_weapon_pool_amount = 0;
	memset(m_rearm_weapon_pool, 0, sizeof(m_rearm_weapon_pool));
}

void CSupportRearmDlg::DoDataExchange(CDataExchange* pDX) {
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_DISALLOW_SUPPORT_SHIPS, m_disallow_support_ships);
	DDX_Check(pDX, IDC_LIMIT_SUPPORT_REARM_TO_POOL, m_limit_rearm_to_pool);
	DDX_Check(pDX, IDC_SUPPORT_REPAIRS_HULL, m_support_repairs_hull);
	DDX_Check(pDX, IDC_DISALLOW_SUPPORT_REARM, m_disallow_support_rearm);
	DDX_Check(pDX, IDC_ALLOW_SUPPORT_REARM_PRECEDENCE, m_allow_weapon_precedence);
	DDX_Check(pDX, IDC_SUPPORT_REARM_POOL_FROM_LOADOUT, m_rearm_pool_from_loadout);
	DDX_Text(pDX, IDC_MAX_HULL_REPAIR_VAL, m_max_hull_repair_val);
	DDV_MinMaxFloat(pDX, m_max_hull_repair_val, 0.0f, 100.0f);
	DDX_Text(pDX, IDC_MAX_SUBSYS_REPAIR_VAL, m_max_subsys_repair_val);
	DDV_MinMaxFloat(pDX, m_max_subsys_repair_val, 0.0f, 100.0f);
	DDX_Text(pDX, IDC_SUPPORT_REARM_POOL_AMOUNT, m_weapon_pool_amount);
}

BEGIN_MESSAGE_MAP(CSupportRearmDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_SUPPORT_REARM_WEAPON_LIST, OnSelchangeWeaponList)
	ON_BN_CLICKED(IDC_SUPPORT_REARM_SET_AMOUNT, OnSetPoolAmount)
	ON_BN_CLICKED(IDC_SUPPORT_REARM_SET_UNLIMITED, OnSetPoolUnlimited)
	ON_BN_CLICKED(IDC_SUPPORT_REARM_SET_ZERO, OnSetPoolZero)
END_MESSAGE_MAP()

BOOL CSupportRearmDlg::OnInitDialog() {
	m_disallow_support_ships = (The_mission.support_ships.max_support_ships == 0) ? TRUE : FALSE;
	m_limit_rearm_to_pool = The_mission.flags[Mission::Mission_Flags::Limited_support_rearm_pool] ? TRUE : FALSE;
	m_support_repairs_hull = The_mission.flags[Mission::Mission_Flags::Support_repairs_hull] ? TRUE : FALSE;
	m_disallow_support_rearm = The_mission.support_ships.disallow_rearm ? TRUE : FALSE;
	m_allow_weapon_precedence = The_mission.support_ships.allow_rearm_weapon_precedence ? TRUE : FALSE;
	m_rearm_pool_from_loadout = The_mission.support_ships.rearm_pool_from_loadout ? TRUE : FALSE;
	m_max_hull_repair_val = The_mission.support_ships.max_hull_repair_val;
	m_max_subsys_repair_val = The_mission.support_ships.max_subsys_repair_val;
	memcpy(m_rearm_weapon_pool, The_mission.support_ships.rearm_weapon_pool, sizeof(m_rearm_weapon_pool));

	CDialog::OnInitDialog();

	populate_weapon_list();
	auto* weapon_list = (CListBox*)GetDlgItem(IDC_SUPPORT_REARM_WEAPON_LIST);
	if (weapon_list != nullptr && weapon_list->GetCount() > 0) {
		weapon_list->SetCurSel(0);
	}
	update_weapon_amount_display();
	UpdateData(FALSE);

	return TRUE;
}

CString CSupportRearmDlg::format_weapon_pool_entry(int weapon_class) const {
	CString text;
	const auto& wi = Weapon_info[weapon_class];
	const int amount = m_rearm_weapon_pool[weapon_class];
	if (amount < 0) {
		text.Format("%s\tUnlimited", wi.name);
	} else {
		text.Format("%s\t%d", wi.name, amount);
	}
	return text;
}

void CSupportRearmDlg::populate_weapon_list() {
	auto* weapon_list = (CListBox*)GetDlgItem(IDC_SUPPORT_REARM_WEAPON_LIST);
	if (weapon_list == nullptr) {
		return;
	}

	weapon_list->ResetContent();
	for (int i = 0; i < weapon_info_size(); ++i) {
		int list_index = weapon_list->AddString(format_weapon_pool_entry(i));
		weapon_list->SetItemData(list_index, i);
	}
}

int CSupportRearmDlg::get_selected_weapon_class() const {
	auto* weapon_list = (CListBox*)GetDlgItem(IDC_SUPPORT_REARM_WEAPON_LIST);
	if (weapon_list == nullptr) {
		return -1;
	}

	int sel = weapon_list->GetCurSel();
	if (sel == LB_ERR) {
		return -1;
	}

	return (int)weapon_list->GetItemData(sel);
}

void CSupportRearmDlg::update_weapon_amount_display() {
	int weapon_class = get_selected_weapon_class();
	if (weapon_class < 0 || weapon_class >= weapon_info_size()) {
		m_weapon_pool_amount = 0;
	} else {
		m_weapon_pool_amount = m_rearm_weapon_pool[weapon_class];
	}

	UpdateData(FALSE);
}

void CSupportRearmDlg::set_selected_weapon_amount(int amount) {
	auto* weapon_list = (CListBox*)GetDlgItem(IDC_SUPPORT_REARM_WEAPON_LIST);
	if (weapon_list == nullptr) {
		return;
	}

	const int sel = weapon_list->GetCurSel();
	const int weapon_class = get_selected_weapon_class();
	if (sel == LB_ERR || weapon_class < 0 || weapon_class >= weapon_info_size()) {
		return;
	}

	if (Weapon_info[weapon_class].disallow_rearm) {
		amount = 0;
	} else if (amount < 0) {
		amount = -1;
	}

	m_rearm_weapon_pool[weapon_class] = amount;
	weapon_list->DeleteString(sel);
	weapon_list->InsertString(sel, format_weapon_pool_entry(weapon_class));
	weapon_list->SetItemData(sel, weapon_class);
	weapon_list->SetCurSel(sel);
	update_weapon_amount_display();
}

void CSupportRearmDlg::OnSelchangeWeaponList() {
	update_weapon_amount_display();
}

void CSupportRearmDlg::OnSetPoolAmount() {
	if (!UpdateData(TRUE)) {
		return;
	}

	set_selected_weapon_amount(m_weapon_pool_amount);
}

void CSupportRearmDlg::OnSetPoolUnlimited() {
	set_selected_weapon_amount(-1);
}

void CSupportRearmDlg::OnSetPoolZero() {
	set_selected_weapon_amount(0);
}

void CSupportRearmDlg::OnOK() {
	if (!UpdateData(TRUE)) {
		return;
	}

	The_mission.support_ships.max_support_ships = (m_disallow_support_ships != FALSE) ? 0 : -1;
	The_mission.flags.set(Mission::Mission_Flags::Limited_support_rearm_pool, m_limit_rearm_to_pool != FALSE);
	The_mission.flags.set(Mission::Mission_Flags::Support_repairs_hull, m_support_repairs_hull != FALSE);
	The_mission.support_ships.disallow_rearm = (m_disallow_support_rearm != FALSE);
	The_mission.support_ships.allow_rearm_weapon_precedence = (m_allow_weapon_precedence != FALSE);
	The_mission.support_ships.rearm_pool_from_loadout = (m_rearm_pool_from_loadout != FALSE);
	The_mission.support_ships.max_hull_repair_val = m_max_hull_repair_val;
	The_mission.support_ships.max_subsys_repair_val = m_max_subsys_repair_val;
	memcpy(The_mission.support_ships.rearm_weapon_pool, m_rearm_weapon_pool, sizeof(m_rearm_weapon_pool));

	for (int i = 0; i < weapon_info_size(); ++i) {
		if (Weapon_info[i].disallow_rearm) {
			The_mission.support_ships.rearm_weapon_pool[i] = 0;
		}
	}

	CDialog::OnOK();
}
