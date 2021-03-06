//------10--------20--------30--------40--------50--------60--------70--------//
//-------|---------|---------|---------|---------|---------|---------|--------//
//----------------------------------------------------------------------------//
// x23RFPCDlg.h : ヘッダー ファイル
#pragma once
//----------------------------------Data--------------------------------------//
#include "MyData.h"
//----------------------------------------------------------------------------//



using namespace std;

// Cx23RFPCDlg ダイアログ
class Cx23RFPCDlg : public CDialogEx
{
	// コンストラクション
public:
	Cx23RFPCDlg(CWnd* pParent = nullptr);	// 標準コンストラクター

// ダイアログ データ
//#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_X23RFPC_DIALOG };
	//#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV サポート


// 実装
protected:
	HICON m_hIcon;

	// 生成された、メッセージ割り当て関数
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

public:
	CEdit msgED;	// System Message
	CButton graphPICT;
	//----------------------------------Button---------------------------------//
	void ButtonManager();
	int pushedNo;
	int pushedCounter;

	CButton* buttons[9];
	CButton button1;
	CButton button2;
	CButton button3;
	CButton button4;
	CButton button5;
	CButton button6;
	CButton button7;
	CButton button8;

	afx_msg void OnBnClickedButton1();	// Start
	afx_msg void OnBnClickedButton2();	// Stop
	afx_msg void OnBnClickedButton3();	// OUTPUT Data
	afx_msg void OnBnClickedButton4();	// Triger something
	afx_msg void OnBnClickedButton5();	// Start Measurement
	afx_msg void OnBnClickedButton6();	// End Measurement
	afx_msg void OnBnClickedButton7();	// Retry
	afx_msg void OnBnClickedButton8();	// Next Subject
	//------------------------------------------------------------------------//
	LRESULT OnMessageRCV(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();

	//----------------------------------Data----------------------------------//
public:
	bool dataFlag = false;
	void dataCallback();
	void updateData();
private:
	Data data_container;
	//------------------------------------------------------------------------//
	//---------------------------------OpenGL---------------------------------//
public:
	bool glFlag = false;						// OpenGL Thread Flag
	void glCallback();					// OpenGL Callback Function
	BOOL SetUpPixelFormat(HDC hdc);		// Setup Pixel Format in Picture Control
	void glSetup(); 					// Setup OpenGL & OpenGL Context
	
	void switchScene(int scene);
private:
	CStatic m_glView;
	CDC* m_pDC;							// Picture Control's Device Context
	HGLRC m_GLRC;						// OpenGL's Device Context
	//------------------------------------------------------------------------//
	//--------------------------------Graph-----------------------------------//
public:
	bool graphFlag = false;
	void graphCallback();
	void drawGraph();
	//------------------------------------------------------------------------//
	//-------------------------------Text-------------------------------------//
public:
	bool textFlag = false;
	void textCallback();
	void drawText();
	void setTextStyle();
private:
	CFont* m_newFont;
	int fontTime;
	CEdit msgED2;	// AS
	CEdit msgED3;	// STL
	CEdit msgED4;	// Other Data
	CEdit msgED5;	// ELEGANCE
	CEdit msgED6;	// LEARNED
	CEdit msgED7;	// num of trial
	//------------------------------------------------------------------------//
	//------------------------------Evaluation--------------------------------//
public:
	bool evaluationFlag = false;
	void evaluationCallback();
	//------------------------------------------------------------------------//
	
};
