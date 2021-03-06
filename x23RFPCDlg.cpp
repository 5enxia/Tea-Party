//------10--------20--------30--------40--------50--------60--------70--------80
//-------|---------|---------|---------|---------|---------|---------|--------//
//----------------------------------------------------------------------------//
// x23RFPCDlg.cpp : 実装ファイル

#include "stdafx.h"
#include "x23RFPC.h"
#include "x23RFPCDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace std;

//----------------------------------Data--------------------------------------//
random_device seed_gen;
mt19937 engine(seed_gen());
const int LIMIT_STL = 1;
//const int LIMIT_AS = 1e10;
const int LIMIT_AS = 100;
//normal_distribution<> distX(0, LIMIT_STL), distY(0, LIMIT_AS);
normal_distribution<> distX(0, LIMIT_STL);
//uniform_int_distribution<> distX(0, LIMIT_STL), distY(0, LIMIT_AS);
uniform_int_distribution<> distY(0, LIMIT_AS);
//----------------------------------------------------------------------------//
//----------------------------------OpenGL------------------------------------//
#include "MyViewer.h"
//----------------------------------------------------------------------------//
//----------------------------------Graph-------------------------------------//
#include "MyGraph.h"
//----------------------------------------------------------------------------//
//----------------------------------Evaluation-------------------------------//
#include "MyEvaluation.h"
Evaluation* evaluation;
//----------------------------------------------------------------------------//



#define WM_RCV (WM_USER+1)
//#define CCPX23RF								// for CCX23MP RF Hybrid Device

#define MaxComPort	99							// 識別できるシリアルポート番号の限界
#define DefaultPort 4

int RScomport = DefaultPort;					// default COM port #4
CString ParamFileName = _T("X23RF_GRAPH.txt");	// テキストファイルにて下記の情報を入れておく
												// シリアル通信ポートの番号
												// データファイル書き出し先のディレクトリ名
												// ※テキストエディタにて内容を変更できる

CString DefaultDir = _T("D:\\");				// データファイルを書き出す際のデフォルトディレクトリ
CString CurrentDir = _T("D:\\");				// Parameter Fileの内容で上書きされる

HANDLE	hCOMnd = INVALID_HANDLE_VALUE;			// 非同期シリアル通信用ファイルハンドル
OVERLAPPED rovl, wovl;							// 非同期シリアル通信用オーバーラップド構造体

HWND hDlg;										// ダイアログ自体のウィンドウハンドルを格納する変数
HANDLE serialh;									// 通信用スレッドのハンドル

int rf;											// 通信用スレッドの走行状態制御用変数
int datasize;									// 正しく受信したデータのサンプル数

#define DATASORT 10								// データの種類は10種類
												// 32-b float W, AS, AATL, Wx, Wy, Wz, STL1, STL2, STL3
												// 16-b unsigned int tm1

												// databuf[SORT][i];
												// 0 -> SEQ

#define PACKETSIZE 41							// ワイヤレス通信における１パケットあたりのデータバイト数
												// PREAMBLE 1
												// SEQ 1
												// data body 36 + 2
												// checksum 1

#define PREAMBLE 0x65							// パケットの先頭を示すデータ（プリアンブル）

#define SAMPLING 2							// サンプリング周波数 [Hz]
#define MAXDURATION 3600						// データの記録は１時間（３６００秒）までとする
#define MAXDATASIZE (SAMPLING*DATASORT*MAXDURATION)

//#define SamplePerPacket 4						// 1パケットあたり4サンプル

float databuf[DATASORT + 1][MAXDATASIZE];			// グローバル変数配列にセンサデータを格納する
int errcnt;										// 通信エラーの回数を数える変数

int xaxis;										// グラフ描画時の時間幅　（単位：秒）
int yaxis;										// グラブ描画を行う軸の番号（1～DATASORT）
int gain;										// グラフの縦軸の拡大倍率

int firsttime;									// パケットエラーカウンターの初期化用変数

static void CloseComPort(void)
// オープンしているシリアルポートをクローズする
{
	if (hCOMnd == INVALID_HANDLE_VALUE)
		return;
	CloseHandle(hCOMnd);
	hCOMnd = INVALID_HANDLE_VALUE;
}

static DWORD OpenComPort(int port)
// portにて指定した番号のシリアルポートをオープンする（非同期モード）
{
	CString ComPortNum;
	COMMPROP	myComProp;
	DCB	myDCB;
	COMSTAT	myComStat;

	// 非同期ＩＯモードなのでタイムアウトは無効
	//	_COMMTIMEOUTS myTimeOut;

	if ((port < 0) || (port > MaxComPort))
		return -1;
	if (port < 10) {
		ComPortNum.Format(_T("COM%d"), port);
	}
	else {
		ComPortNum.Format(_T("\\\\.\\COM%d"), port);	// Bill Gates' Magic ...
	}

	ZeroMemory(&rovl, sizeof(rovl));
	ZeroMemory(&wovl, sizeof(wovl));
	rovl.Offset = 0;
	wovl.Offset = 0;
	rovl.OffsetHigh = 0;
	wovl.OffsetHigh = 0;
	rovl.hEvent = NULL;
	wovl.hEvent = NULL;

	hCOMnd = CreateFile((LPCTSTR)ComPortNum, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	// COMポートをオーバラップドモード（非同期通信モード）にてオープンしている

	if (hCOMnd == INVALID_HANDLE_VALUE) {
		return -1;
	}

	GetCommProperties(hCOMnd, &myComProp);
	GetCommState(hCOMnd, &myDCB);
	//	if( myComProp.dwSettableBaud & BAUzD_128000)
	//		myDCB.BaudRate = CBR_128000;
	//	else
	myDCB.BaudRate = CBR_115200;		// 115.2KbpsモードをWindowsAPIは正しく認識しない
										//	myDCB.BaudRate = CBR_9600;
	myDCB.fDtrControl = DTR_CONTROL_DISABLE;
	myDCB.Parity = NOPARITY;
	myDCB.ByteSize = 8;
	myDCB.StopBits = ONESTOPBIT;
	myDCB.fDsrSensitivity = FALSE;
	SetCommState(hCOMnd, &myDCB);
	DWORD	d;

	d = myComProp.dwMaxBaud;

	DWORD	myErrorMask;
	char	rbuf[32];
	DWORD	length;

	// オーバーラップドモードでは、タイムアウト値は意味をなさない

	//	GetCommTimeouts( hCOMnd, &myTimeOut);
	//	myTimeOut.ReadTotalTimeoutConstant = 10;	// 10 msec
	//	myTimeOut.ReadIntervalTimeout = 200;	// 200 msec
	//	SetCommTimeouts( hCOMnd, &myTimeOut);
	//	GetCommTimeouts( hCOMnd, &myTimeOut);
	//	ReadTimeOut = (int)myTimeOut.ReadTotalTimeoutConstant;

	ClearCommError(hCOMnd, &myErrorMask, &myComStat);

	if (myComStat.cbInQue > 0) {
		int	cnt;
		cnt = (int)myComStat.cbInQue;
		for (int i = 0; i < cnt; i++) {
			// Synchronous IO
			//			ReadFile( hCOMnd, rbuf, 1, &length, NULL);		
			//

			// オーバーラップドモードで、同期通信的なことを行うためのパッチコード
			ReadFile(hCOMnd, rbuf, 1, &length, &rovl);
			while (1) {
				if (HasOverlappedIoCompleted(&rovl)) break;
			}
		}
	}

	return d;
}


// コールバック関数によるシリアル通信状況の通知処理

int rcomp;

VOID CALLBACK FileIOCompletionRoutine(DWORD err, DWORD n, LPOVERLAPPED ovl)
{
	rcomp = n;			// 読み込んだバイト数をそのままグローバル変数に返す
}

int wcomp;

VOID CALLBACK WriteIOCompletionRoutine(DWORD err, DWORD n, LPOVERLAPPED ovl)
{
	wcomp = n;
}

// 無線パケットを受信するためのスレッド

#define RINGBUFSIZE 1024		// 受信用リングバッファのサイズ（バイト数）

unsigned __stdcall serialchk(VOID* dummy)
{
	DWORD myErrorMask;						// ClearCommError を使用するための変数
	COMSTAT	 myComStat;						// 受信バッファのデータバイト数を取得するために使用する
	unsigned char buf[RINGBUFSIZE];			// 無線パケットを受信するための一時バッファ

	unsigned char ringbuffer[RINGBUFSIZE];	// パケット解析用リングバッファ
	int rpos, wpos, rlen;					// リングバッファ用ポインタ（read, write)、データバイト数
	int rpos_tmp;							// データ解析用read位置ポインタ
	int rest;								// ClearCommErrorにて得られるデータバイト数を記録する

	int seq, expected_seq;					// モーションセンサから受信するシーケンス番号（８ビット）
											// と、その期待値（エラーがなければ受信する値）

	unsigned char packet[PACKETSIZE];		// パケット解析用バッファ （１パケット分）
	int i, j, chksum, tmp;
	float* f_p;

	//	unsigned char wbuf[16];					// Acknowledge用の送信バッファ
	//	DWORD	length;

	//	wbuf[0] = 'M';							// モーションデータ送信継続要求コマンド

	expected_seq = 0;						// 最初に受信されるべきSEQの値はゼロ
	rpos = wpos = 0;						// リングバッファの読み出し及び書き込みポインタを初期化
	rlen = 0;								// リングバッファに残っている有効なデータ数（バイト）

	while (rf) {
		rcomp = 0;					// FileIOCompletionRoutineが返す受信バイト数をクリアしておく
									// まずは無線パケットの先頭の１バイトを読み出しに行く
		ReadFileEx(hCOMnd, buf, 1, &rovl, FileIOCompletionRoutine);

		while (1) {
			SleepEx(100, TRUE);	// 最大で100ミリ秒 = 0.1秒の間スリープするが、その間に
			if (rcomp == 1) break;	// I/O完了コールバック関数が実行されるとスリープを解除する

			if (!rf) {				// 外部プログラムからスレッドの停止を指示された時の処理
				CancelIo(hCOMnd);	// 発行済みのReadFileEx操作を取り消しておく
				break;
			}
			// データが送られてこない時間帯では、受信スレッド内のこの部分
			// が延々と処理されているが、大半がSleepExの実行に費やされる
			// ことで、システムに与える負荷を軽減している。
		}

		if (!rf) break;				// 外部プログラムからスレッドの停止を指示された

		ringbuffer[wpos] = buf[0];	// 最初の１バイトが受信された
		wpos++;						// リングバッファの書き込み位置ポインタを更新
		wpos = (wpos >= RINGBUFSIZE) ? 0 : wpos;	// １周していたらポインタをゼロに戻す（なのでRING）
		rlen++;						// リングバッファ内の有効なデータ数を＋１する

		ClearCommError(hCOMnd, &myErrorMask, &myComStat);	// 受信バッファの状況を調べるＡＰＩ

		rest = myComStat.cbInQue;	// 受信バッファに入っているデータバイト数が得られる

		if (rest == 0) continue;		// 何も入っていなかったので次の１バイトを待ちにいく

		rcomp = 0;
		ReadFileEx(hCOMnd, buf, rest, &rovl, FileIOCompletionRoutine);
		// 受信バッファに入っているデータを読み出す
		// 原理的にはrestで示される数のデータを受信することができるが、
		// 万一に備えてデータが不足してしまった時の処理を考える。

		SleepEx(16, TRUE);			// Windowsにおけるシリアルポートの標準レイテンシ（16msec）だけ待つ
		if (rcomp != rest) {
			CancelIo(hCOMnd);		// ClearCommErrorで取得したデータバイト数に満たない
		}							// データしか受信されなかったので、先に発行したReadFileEx
									// をキャンセルしている

		i = 0;
		while (rcomp > 0) {			// rcompには読み出すことのできたデータのバイト数が入っている
			ringbuffer[wpos] = buf[i];	// リングバッファに受信データを転送する
			wpos++;
			wpos = (wpos >= RINGBUFSIZE) ? 0 : wpos;
			rlen++;
			i++;
			rcomp--;
		}

		// ここからパケット解析に入る

		while (1) {					// 有効なパケットである限り解析を継続する
			while (rlen > 0) {
				if (ringbuffer[rpos] == PREAMBLE) break;
				rpos++;								// 先頭がPREAMBLEではなかった
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;								// 有効なデータ数を１つ減らして再度先頭を調べる
			}

			if (rlen < PACKETSIZE) break;	// 解析に必要なデータバイト数に達していなかったので
											// 最初の１バイトを待つ処理に戻る

			rpos_tmp = rpos;	// リングバッファを検証するための仮ポインタ
								// まだリングバッファ上にあるデータが有効であると分かったわけではない

			for (i = 0, chksum = 0; i < (PACKETSIZE - 1); i++) {
				packet[i] = ringbuffer[rpos_tmp];	// とりあえず解析用バッファにデータを整列させる
				chksum += packet[i];
				rpos_tmp++;
				rpos_tmp = (rpos_tmp >= RINGBUFSIZE) ? 0 : rpos_tmp;
			}

			if ((chksum & 0xff) != ringbuffer[rpos_tmp]) {	// チェックサムエラーなのでパケットは無効
				rpos++;										// 先頭の１バイトを放棄する
				rpos = (rpos >= RINGBUFSIZE) ? 0 : rpos;
				rlen--;
				continue;	// 次のPREAMBLEを探しにいく
			}

			// PREAMBLE、チェックサムの値が正しいPACKETSIZ長のデータがpacket[]に入っている

			// WriteFile(hCOMnd, wbuf, 1, &length, &wovl);		// Acknowledgeパケットを非同期モードで送出する

			seq = packet[1];
			databuf[0][datasize] = (float)seq;		// seq は８ビットにて0～255の間を1ずつカウントアップしていく数値

			if (firsttime) {
				firsttime = 0;
			}
			else {
				if (seq != expected_seq) {		// 受信されたseqが、１つ前のseqに+1したものであることをチェックする
					errcnt += (seq + 256 - expected_seq) % 256;	// パケットエラー数を更新する
				}
			}
			expected_seq = (seq + 1) % 256;					// 次のseqの期待値をexpected_seqに入れる

#if 0
			for (i = 0; i < SamplePerPacket; i++) {
				for (j = 0; j < 6; j++) {
					tmp = ((packet[i * 12 + 2 + j * 2] & 0x00ff) << 8) + (packet[i * 12 + j * 2 + 3] & 0xff);
					databuf[j + 1][datasize + i] = (tmp >= 32768) ? (tmp - 65536) : tmp;
				}
				tmp = ((packet[50] & 0x00ff) << 8) + (packet[51] & 0xff);
				databuf[7][datasize + i] = (tmp >= 32768) ? (tmp - 65536) : tmp;
			}
#endif

			for (i = 0; i < 9; i++) {
				f_p = (float*)&packet[i * 4 + 2];
				databuf[i + 1][datasize] = *f_p;
			}

			tmp = ((packet[38] & 0x00ff) << 8) + (packet[39] & 0xff);
			databuf[i + 1][datasize] = (float)tmp;

			datasize++;
			datasize = (datasize >= MAXDATASIZE) ? (MAXDATASIZE - 1) : datasize;	// データ数が限界に到達した際の処理

			PostMessage(hDlg, WM_RCV, (WPARAM)1, NULL);	// 100Hzサンプリングであれば0.08秒に1回画面が更新される
														// PostMessage()自体では処理時間は極めて短い

			rpos = (rpos + PACKETSIZE) % RINGBUFSIZE;			// 正しく読み出せたデータをリングバッファから除去する
			rlen -= PACKETSIZE;								// バッファの残り容量を更新する
		}
	}
	_endthreadex(0);	// スレッドを消滅させる
	return 0;
}

// アプリケーションのバージョン情報に使われる CAboutDlg ダイアログ

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

	// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

// 実装
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// Cx23RFPCDlg ダイアログ



Cx23RFPCDlg::Cx23RFPCDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_X23RFPC_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void Cx23RFPCDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_GLVIEW, m_glView);

	DDX_Control(pDX, IDC_EDIT1, msgED);
	DDX_Control(pDX, IDC_EDIT2, msgED2);
	DDX_Control(pDX, IDC_EDIT3, msgED3);

	DDX_Control(pDX, IDCANCEL, graphPICT);
	DDX_Control(pDX, IDC_EDIT4, msgED4);
	DDX_Control(pDX, IDC_EDIT5, msgED5);
	DDX_Control(pDX, IDC_EDIT6, msgED6);
	DDX_Control(pDX, IDC_EDIT7, msgED7);
	DDX_Control(pDX, IDC_BUTTON1, button1);
	DDX_Control(pDX, IDC_BUTTON2, button2);
	DDX_Control(pDX, IDC_BUTTON3, button3);
	DDX_Control(pDX, IDC_BUTTON4, button4);
	DDX_Control(pDX, IDC_BUTTON5, button5);
	DDX_Control(pDX, IDC_BUTTON6, button6);
	DDX_Control(pDX, IDC_BUTTON7, button7);
	DDX_Control(pDX, IDC_BUTTON8, button8);
}


BEGIN_MESSAGE_MAP(Cx23RFPCDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON1, &Cx23RFPCDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &Cx23RFPCDlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &Cx23RFPCDlg::OnBnClickedButton3)
	ON_MESSAGE(WM_RCV, &Cx23RFPCDlg::OnMessageRCV)
	ON_BN_CLICKED(IDC_BUTTON4, &Cx23RFPCDlg::OnBnClickedButton4)
	ON_BN_CLICKED(IDC_BUTTON5, &Cx23RFPCDlg::OnBnClickedButton5)
	ON_BN_CLICKED(IDC_BUTTON6, &Cx23RFPCDlg::OnBnClickedButton6)
	ON_BN_CLICKED(IDC_BUTTON8, &Cx23RFPCDlg::OnBnClickedButton8)
	ON_BN_CLICKED(IDC_BUTTON7, &Cx23RFPCDlg::OnBnClickedButton7)
END_MESSAGE_MAP()



// Cx23RFPCDlg メッセージ ハンドラー

BOOL Cx23RFPCDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// "バージョン情報..." メニューをシステム メニューに追加します。

	hDlg = this->m_hWnd;		// このダイアログへのハンドルを取得する
								// このコードは手入力している


	// IDM_ABOUTBOX は、システム コマンドの範囲内になければなりません。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// このダイアログのアイコンを設定します。アプリケーションのメイン ウィンドウがダイアログでない場合、
	//  Framework は、この設定を自動的に行います。
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンの設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンの設定



	// TODO: 初期化をここに追加します。

	// 初期化コードを手入力にて追加している

	//CButton* radio1 = (CButton*)GetDlgItem(IDC_RADIO1);
	//radio1->SetCheck(1);	// Y-axis : ax

	yaxis = 2;				// 起動時にASのボタンが押されている状態にする

	//CButton* radio7 = (CButton*)GetDlgItem(IDC_RADIO7);
	//radio7->SetCheck(1);	// X-axis : 5sec
	xaxis = 9;				// 起動時にSTLのボタンが押されている状態にする

	//gain = 1;				// グラフ縦軸のゲインは１に設定
	gain = 100;

	rf = 0;			// 無線パケット受信スレッドは停止
	datasize = 0;	// モーションデータの数を０に初期化

	firsttime = 1;	// 受信パケット未到来

					// 設定ファイルを読み込み、シリアル通信用ポート番号とファイル保存先フォルダを設定する
	CStdioFile pFile;
	CString buf;
	char pbuf[256], dirbuf[_MAX_PATH];
	char pathname[256];
	int i, comport, dirlen;

	for (i = 0; i < 256; i++) {
		pbuf[i] = pathname[i] = 0x00;	// 安全のためNULLで埋めておく
	}

	if (!pFile.Open(ParamFileName, CFile::modeRead)) {
		msgED.SetWindowTextW(_T("Parameter file not found..."));
		RScomport = DefaultPort;
		dirlen = GetCurrentDirectory(_MAX_PATH, (LPTSTR)dirbuf);
		if (dirlen != 0) {
			CurrentDir.Format(_T("%s\\"), dirbuf);
		}
	}
	else {
		pFile.ReadString((LPTSTR)pbuf, 32);
		dirlen = GetCurrentDirectory(_MAX_PATH, (LPTSTR)dirbuf);

		if (dirlen != 0) {
			CurrentDir.Format(_T("%s\\"), dirbuf);
		}

		pbuf[1] = pbuf[2];	// Unicodeに対応するためのパッチコード
		pbuf[2] = 0x00;

		comport = atoi(pbuf);	// ポート番号をテキストファイルから取得している

		CString cpath;
		pFile.ReadString(cpath);

		if (cpath.GetLength() != 0) {
			DefaultDir = cpath;
		}

		if ((comport > 0) && (comport <= MaxComPort)) {
			RScomport = comport;
			buf.Format(_T("Parameter File Found. RS channel %d"), comport);
			msgED.SetWindowTextW(buf);
		}
		else {
			buf.Format(_T("Invalid Comport number! [COM1-COM%d are valid]"), MaxComPort);
			msgED.SetWindowTextW(buf);
			RScomport = DefaultPort;	// default port number
		}
		pFile.Close();
	}

	//----------------------------------Text------------------------------------//
	setTextStyle();
	//--------------------------------------------------------------------------//
	//------------------------------Evaluation----------------------------------//
	evaluation = new Evaluation();
	//--------------------------------------------------------------------------//
	//----------------------------------Button----------------------------------//
	buttons[1] = &button1;
	buttons[2] = &button2;
	buttons[3] = &button3;
	buttons[4] = &button4;
	buttons[5] = &button5;
	buttons[6] = &button6;
	buttons[7] = &button7;
	buttons[8] = &button8;

	for (int i = 1; i < size(buttons); i++) buttons[i]->EnableWindow(false);
	buttons[1]->EnableWindow(true);

	pushedNo = 0;
	pushedCounter = 0;
	//--------------------------------------------------------------------------//

	return TRUE;  // フォーカスをコントロールに設定した場合を除き、TRUE を返します。
}

void Cx23RFPCDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// ダイアログに最小化ボタンを追加する場合、アイコンを描画するための
//  下のコードが必要です。ドキュメント/ビュー モデルを使う MFC アプリケーションの場合、
//  これは、Framework によって自動的に設定されます。

void Cx23RFPCDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 描画のデバイス コンテキスト

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// クライアントの四角形領域内の中央
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// アイコンの描画
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// ユーザーが最小化したウィンドウをドラッグしているときに表示するカーソルを取得するために、
//  システムがこの関数を呼び出します。
HCURSOR Cx23RFPCDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

int clipping(int y, int ymin, int ymax)
{
	if (ymin >= ymax) return 0; // fatal error!

	y = (y < ymin) ? ymin : y;
	y = (y > ymax) ? ymax : y;
	return y;
}

LRESULT Cx23RFPCDlg::OnMessageRCV(WPARAM wParam, LPARAM lParam)
// グラフを描画するためのイベント駆動型コード（システムからコールバックされる）
{
	CString s;
	static int rcnt = 0;

	// 受信したパケット数と無線通信エラーの回数を表示する
	rcnt++;

	s.Format(_T("Received : %d Error = %d"), rcnt, errcnt);
	msgED.SetWindowTextW(s);

	//------------------------------------------------------------------------//
	//--------------------------------Graph-----------------------------------//
	drawGraph();
	//------------------------------------------------------------------------//
	updateData();
	//---------------------------------Text-----------------------------------//
	drawText();
	//------------------------------------------------------------------------//
	//------------------------------evaluation-------------------------------//
	if (evaluationFlag) {
		evaluation->updateAS(data_container.getAS());
		evaluation->updateSTL(data_container.getSTL().z);
	}
	//------------------------------------------------------------------------//
	return TRUE;
}

void Cx23RFPCDlg::OnDestroy()
{
	CDialog::OnDestroy();
	//-------------------------------OpenGL-----------------------------------//
	glFlag = false;
	wglMakeCurrent(NULL, NULL); // Disable Windows OpenGL Handler
	wglDeleteContext(m_GLRC); // Delete Winddows OpenGL Context 
	delete m_pDC;
	//------------------------------------------------------------------------//
	//---------------------------------Text-----------------------------------//
	textFlag = false;
	m_newFont->DeleteObject(); // Destroy m_newFont Object
	delete m_newFont; // Delelte m_newFont 
	//------------------------------------------------------------------------//
	//---------------------------------Data-----------------------------------//
	dataFlag = false;
	//------------------------------------------------------------------------//
	//-----------------------------------Graph--------------------------------//
	graphFlag = false;
	//------------------------------------------------------------------------//
}



void Cx23RFPCDlg::ButtonManager()
{
	pushedCounter++;
	if (pushedNo == 1) pushedCounter = 1;
	buttons[pushedNo]->EnableWindow(false);
	if (pushedCounter == 1) {
		buttons[2]->EnableWindow(true);
		buttons[5]->EnableWindow(true);
	}
	if (pushedCounter == 2) {
		if (pushedNo == 2) {
			buttons[5]->EnableWindow(false);
			buttons[1]->EnableWindow(true);
			buttons[3]->EnableWindow(true);
		}
		else {
			buttons[2]->EnableWindow(false);
			buttons[6]->EnableWindow(true);
		}
	}
	if (pushedCounter == 3) {
		if (pushedNo == 3) {
			buttons[1]->EnableWindow(true);
		}
		else buttons[5]->EnableWindow(true);
	}
	if (pushedCounter == 4) buttons[6]->EnableWindow(true);
	if (pushedCounter == 5) buttons[2]->EnableWindow(true);
	if (pushedCounter == 6) {
		buttons[3]->EnableWindow(true);
		buttons[7]->EnableWindow(true);
		buttons[8]->EnableWindow(true);
	}
	if (pushedCounter == 7) {
		if(pushedNo == 7){
			buttons[8]->EnableWindow(false);
		}else buttons[7]->EnableWindow(false);
		buttons[1]->EnableWindow(true);
	}
}

void Cx23RFPCDlg::OnBnClickedButton1()
{
	pushedNo = 1;
	ButtonManager();

	// START
	DWORD d;
	unsigned char wbuf[16];
	DWORD	length;

#ifdef CCPX23RF

	if (rf) {
		//		msgED.SetWindowTextW(_T("Thread is already running..."));
		//		return;
		wbuf[0] = 'M';
		//		WriteFile(hCOMnd, wbuf, 1, &length, &wovl);
		WriteFileEx(hCOMnd, wbuf, 1, &wovl, WriteIOCompletionRoutine);
		return;
	}
#endif

	d = OpenComPort(RScomport);

	if (d < 0) {
		msgED.SetWindowTextW(_T("can't initialize COM port"));
		Invalidate();
		UpdateWindow();
		return;
	}

	rf = 1;
	errcnt = 0;

	unsigned int tid;
	serialh = (HANDLE)_beginthreadex(NULL, 0, serialchk, NULL, 0, &tid);

	if (serialh != NULL) {
		msgED.SetWindowTextW(_T("Start Recording"));
		//---------------------------------OpenGL-------------------------------------//
		glFlag = true;
		std::thread glThread(&Cx23RFPCDlg::glCallback, this);
		glThread.detach();
		//----------------------------------------------------------------------------//
		//---------------------------------Data---------------------------------------//
		// dataFlag = true;
		// thread testThread(&Cx23RFPCDlg::dataCallback, this);
		// testThread.detach();
		//----------------------------------------------------------------------------//
		//---------------------------------Text---------------------------------------//
#ifdef _DEBUG
		textFlag = true;
		thread textThread(&Cx23RFPCDlg::textCallback, this);
		textThread.detach();
#endif
		//----------------------------------------------------------------------------//
#ifdef CCPX23RF
		wbuf[0] = 'M';
		WriteFile(hCOMnd, wbuf, 1, &length, &wovl);
#endif
	}
	else {
		rf = 0;
		CloseComPort();
		msgED.SetWindowTextW(_T("Thread is not running..."));
	}
}
void Cx23RFPCDlg::OnBnClickedButton2()
{
	pushedNo = 2;
	ButtonManager();


	// STOP
	unsigned char wbuf[16];
	DWORD	length;

	if (rf) {
		rf = 0;
		firsttime = 1;
#ifdef CCPX23RF
		wbuf[0] = 'Q';
		WriteFileEx(hCOMnd, wbuf, 1, &wovl, WriteIOCompletionRoutine);
		//		WriteFile(hCOMnd, wbuf, 1, &length, &wovl);
#endif

		DWORD dwExitCode;
		while (1) {
			GetExitCodeThread(serialh, &dwExitCode);
			if (dwExitCode != STILL_ACTIVE) break;
		}
		CloseHandle(serialh);
		serialh = NULL;
		CloseComPort();
		msgED.SetWindowTextW(_T("Stop Recording"));
		//-----------------------------------Data-------------------------------------//
		dataFlag = false;
		//----------------------------------------------------------------------------//
		//---------------------------------OpenGL-------------------------------------//
		glFlag = false;
		//----------------------------------------------------------------------------//
		//-----------------------------------Graph------------------------------------//
		graphFlag = false;
		//----------------------------------------------------------------------------//
		//-----------------------------------Text-------------------------------------//
		textFlag = false;
		//----------------------------------------------------------------------------//
	}
	else {
		//		msgED.SetWindowTextW(_T("Recording is not running..."));
#ifdef CCPX23RF
		OpenComPort(RScomport);
		wbuf[0] = 'Q';
		WriteFile(hCOMnd, wbuf, 1, &length, &wovl);
		CloseComPort();
#endif
	}

}
void Cx23RFPCDlg::OnBnClickedButton3()
{
	pushedNo = 3;
	ButtonManager();


	// FILE OUT
	DWORD		dwFlags;
	LPCTSTR		lpszFilter = _T("CSV File(*.csv)|*.csv|");
	CString		fn, pathn;
	int			i, j, k;
	CString		rbuf;
	CString		writebuffer;

	if (rf) {
		msgED.SetWindowTextW(_T("Recording thread is still running..."));
		return;
	}

	if (datasize == 0) {
		msgED.SetWindowTextW(_T("There is no data..."));
		return;
	}

	dwFlags = OFN_OVERWRITEPROMPT | OFN_CREATEPROMPT;

	CFileDialog myDLG(FALSE, _T("csv"), NULL, dwFlags, lpszFilter, NULL);
	myDLG.m_ofn.lpstrInitialDir = DefaultDir;


	if (myDLG.DoModal() != IDOK) return;


	CStdioFile fout(myDLG.GetPathName(), CFile::modeWrite | CFile::typeText | CFile::modeCreate);

	pathn = myDLG.GetPathName();
	fn = myDLG.GetFileName();
	j = pathn.GetLength();
	k = fn.GetLength();
	DefaultDir = pathn.Left(j - k);

	msgED.SetWindowTextW(_T("Writing the Data File..."));

	Invalidate();
	UpdateWindow();

	// RAW DATA FORMAT
	for (i = 0; i < datasize; i++) {
		writebuffer.Format(_T("%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n"),
			databuf[0][i],								 // sequence counter
			databuf[1][i], databuf[2][i], databuf[3][i], // W, AS, AATL 
			databuf[4][i], databuf[5][i], databuf[6][i], // Wx, Wy, Wz
			databuf[7][i], databuf[8][i], databuf[9][i], // STL1, STL2, STL3
			databuf[10][i]							     // TMR1
		);
		fout.WriteString((LPCTSTR)writebuffer);
	}

	fout.Close();

	datasize = 0;

	msgED.SetWindowTextW(_T("Motion Data File Writing Succeeded"));
}
void Cx23RFPCDlg::OnBnClickedButton4()
{
	pushedNo = 4;
	ButtonManager();

	// TRIG
//	DWORD d;
	unsigned char wbuf[16];
	DWORD	length;

	if (rf) {
		//		msgED.SetWindowTextW(_T("Thread is already running..."));
		//		return;
#if 0
		wbuf[0] = 'M';
		WriteFileEx(hCOMnd, wbuf, 1, &wovl, WriteIOCompletionRoutine);
		//		WriteFile(hCOMnd, wbuf, 1, &length, &wovl);
		msgED.SetWindowTextW(_T("TRIG"));
#endif
		return;
	}
}

void Cx23RFPCDlg::OnBnClickedButton5()
{
	pushedNo = 5;
	ButtonManager();

	

	// 重複防止
	if (evaluationFlag) {
		msgED.SetWindowTextW(_T("Already Start evaluation"));
		return;
	}
	//------------------------------Evaluation-----------------------------------//
	if (evaluation->getCount() == 0) {
		//First
		msgED.SetWindowTextW(_T("Start First evaluation ..."));
		msgED5.SetWindowTextW(_T("判定中"));	// ELEGANCE
		msgED6.SetWindowTextW(_T("判定中"));	// LEAENING

		// switch OpenGL Scene
		switchScene(0);
	}
	else if (evaluation->getCount() == 1) {
		//Second
		msgED.SetWindowTextW(_T("Start Second evaluation..."));
		msgED5.SetWindowTextW(_T("判定中"));	// ELEGANCE

		// OpenGL Scene
		switchScene(0);
	}
	else {
		// Over
		msgED.SetWindowTextW(_T("Press RETRY or NEXT"));
		return;
	}

	CString str;
	str.Format(_T("ID:%d\r\n\
%d回目"), evaluation->getID(), (evaluation->getCount() + 1));
	msgED7.SetWindowTextW(str);
	
	evaluationFlag = true;
#ifdef _DEBUG
	thread evaluationThread(&Cx23RFPCDlg::evaluationCallback, this);
	evaluationThread.detach();
#endif // _DEBUG
	//----------------------------------------------------------------------------//
	//---------------------------------Graph--------------------------------------//
#ifdef _DEBUG
	graphFlag = true;
	thread graphThread(&Cx23RFPCDlg::graphCallback, this);
	graphThread.detach();
#endif
	//----------------------------------------------------------------------------//
}


void Cx23RFPCDlg::OnBnClickedButton6()
{
	pushedNo = 6;
	ButtonManager();

	// 重複防止
	if (!evaluationFlag) {
		msgED.SetWindowTextW(_T("Already Stop evaluation"));
		return;
	}
	//---------------------------------Graph--------------------------------------//
	graphFlag = false;
	//----------------------------------------------------------------------------//
	//------------------------------Evaluation------------------------------------//
	evaluationFlag = false;
	CString str;
	if (evaluation->getCount() == 0) {
		//First
		msgED.SetWindowTextW(_T("Stop First evaluation..."));
		str = evaluation->getEleganceStr().c_str();
		msgED5.SetWindowTextW(str);	// ELEGANCE
		
		// OpenGL Scene
		switchScene(1);

		evaluation->setResults();
	}
	else if(evaluation->getCount() == 1){
		//Second
		msgED.SetWindowTextW(_T("Stop Second evaluation..."));
		str = evaluation->getEleganceStr().c_str();
		msgED5.SetWindowTextW(str);	// ELEGANCE
		str = evaluation->getLearningStr().c_str();
		msgED6.SetWindowTextW(str);	// ELEGANCE

		// OpenGL Scene
		switchScene(1);

		evaluation->setResults();
		// writeout to CSV
		evaluation->saveResults();
	}
	else {
		// Over
		msgED.SetWindowTextW(_T("Press RETRY or NEXT"));
		return;
	}
	evaluation->increaseCount();
	//----------------------------------------------------------------------------//
}

void Cx23RFPCDlg::OnBnClickedButton7()
{
	pushedNo = 7;
	ButtonManager();

	if (evaluation->getCount() > 1) {
		msgED.SetWindowTextW(_T("Enable New Measuarement"));
		evaluation->retry();
	}
	else {
		msgED.SetWindowTextW(_T("Disabled Now!"));
	}
}
 
void Cx23RFPCDlg::OnBnClickedButton8()
{
	pushedNo = 8;
	ButtonManager();

	if (evaluation->getCount() > 1) {
		delete evaluation;
		evaluation = new Evaluation();
		msgED.SetWindowTextW(_T("Enable Next Measurement"));
		CString str;
		str.Format(_T("ID:%d\r\n\
%d回目"), evaluation->getID(), (evaluation->getCount() + 1));
		msgED7.SetWindowTextW(str);
	}
	else {
		msgED.SetWindowTextW(_T("Disabled Now!"));
	}
}


//----------------------------------Data--------------------------------------//
void Cx23RFPCDlg::dataCallback() {
	while (dataFlag) {
		updateData();
	}
}
void Cx23RFPCDlg::updateData()
{
#ifdef _DEBUG
	data_container.setW(distX(engine));
	data_container.setAS(distY(engine));
	data_container.setAATL(distX(engine));
	data_container.setVecW(distX(engine), distX(engine), distX(engine));
	data_container.setSTL(distX(engine), distX(engine), distX(engine));
	data_container.setTime(distX(engine));
#else
	data_container.setW(databuf[1][datasize - 2]);
	data_container.setAS(databuf[2][datasize - 2]);
	data_container.setAATL(databuf[3][datasize -2 ]);
	data_container.setVecW(databuf[4][datasize -2], databuf[5][datasize-2], databuf[6][datasize-2]);
	data_container.setSTL(databuf[7][datasize-2], databuf[8][datasize-2], databuf[9][datasize-2]);
	data_container.setTime(databuf[10][datasize-2]);
#endif // DEBUG
}
//----------------------------------------------------------------------------//
//----------------------------------OpenGL------------------------------------//
BOOL Cx23RFPCDlg::SetUpPixelFormat(HDC hdc)
{
	PIXELFORMATDESCRIPTOR pfd = {
	sizeof(PIXELFORMATDESCRIPTOR),	// PFD Size
	1,								//Version 
	PFD_DRAW_TO_WINDOW |			// Render to Window
	PFD_SUPPORT_OPENGL |			// Use OpenGL
	PFD_DOUBLEBUFFER,				// Do Double Buffering 
	PFD_TYPE_RGBA,					// RGBA mode
	24,								// Color Buffer 24bit 
	0, 0, 0, 0, 0, 0,				// Don't specyfied bit (each channel)
	0, 0,							// Don't use Alpha buffer
	0, 0, 0, 0, 0,					// Don't user Accmulation Buffer
	32,								// Depth buffer 32bit 
	0,								// Don't use  stencil buffer
	0,								// Don't use sub buffer
	PFD_MAIN_PLANE,					// Main Layer
	0,								// Preserved 
	0, 0, 0							// ignore layer mask
	};

	int pf = ChoosePixelFormat(hdc, &pfd);
	if (pf != 0) return SetPixelFormat(hdc, pf, &pfd);
	return FALSE;
}

void Cx23RFPCDlg::glSetup()
{
	m_pDC = new CClientDC(&m_glView);

	if (SetUpPixelFormat(m_pDC->m_hDC) != FALSE) {
		m_GLRC = wglCreateContext(m_pDC->m_hDC);
		wglMakeCurrent(m_pDC->m_hDC, m_GLRC);

		CRect rc; // Get Rendering Context Size 
		m_glView.GetClientRect(&rc);
		GLint width = rc.Width();
		GLint height = rc.Height();

		Viewer::setup(width, height);
	}
}

void Cx23RFPCDlg::switchScene(int scene)
{
	if (scene == 0) {
		glFlag = true;
		Viewer::scene = scene;
		thread glthread(&Cx23RFPCDlg::glCallback, this);
		glthread.detach();
	}
	else {
		delete m_pDC;
		glFlag = false;
		Viewer::scene = scene;
		Viewer::elegance = evaluation->getEleganceInt();
		Viewer::learning = evaluation->getLearningInt();
		glSetup();
		Viewer::update();
		Viewer::draw();
		SwapBuffers(m_pDC->m_hDC);
	}
}

void Cx23RFPCDlg::glCallback()
{
	glSetup();
	while (glFlag) {
		Viewer::update();
		Viewer::draw();
		SwapBuffers(m_pDC->m_hDC);
	}
}
//----------------------------------------------------------------------------//
//------------------------------------Graph-----------------------------------//
void Cx23RFPCDlg::drawGraph() {
	// グラフを描画する
	CWnd* myPICT = GetDlgItem(IDC_GRAPH);	// 画面上のピクチャコントロールと対応付ける
	CClientDC myPICTDC(myPICT);				// デバイスコンテキストを作成
	CRect myPICTRECT;						// 描画領域を表す四角形（rectangle）の座標

	int i, j, x, y;
	x = 0;
	int w, h;
	int gsamples;

	int radius = 5;

	myPICT->GetClientRect(myPICTRECT);
	w = myPICTRECT.Width();
	h = myPICTRECT.Height();

	myPICTDC.FillSolidRect(myPICTRECT, RGB(255, 255, 255)); //コンテクストを塗りつぶす
	
	//------------------Gri--------------//
	CPen myPEN(PS_DOT, 1, RGB(0, 128, 255));
	CPen* oldPEN = myPICTDC.SelectObject(&myPEN);

	int gx, gy;
	for (int i = 1; i < 10; i++)
	{
		gx = Graph::map(i, 0, 9, 0, w);
		myPICTDC.MoveTo(gx, 0);
		myPICTDC.LineTo(gx, h);
		
		gy = Graph::map(i, 0, 9, 0, h);
		myPICTDC.MoveTo(0, gy);
		myPICTDC.LineTo(w, gy);
	}
	//----------------------------------//

	myPICTDC.SelectObject(oldPEN);	// 使ったペンを元の状態に戻しておく

	CPen myPEN2(PS_DOT,radius * 2, RGB(255, 0, 0));	
	CPen* oldPEN2 = myPICTDC.SelectObject(&myPEN2);

	x = Graph::map(evaluation->getSTL(),0,LIMIT_STL,radius,w - radius);
	y = h - Graph::map(evaluation->getAS(),0,LIMIT_AS,radius,h - radius);
	myPICTDC.MoveTo(x , y);
	myPICTDC.LineTo(x + 1, y + 1);

	myPICTDC.SelectObject(oldPEN2);	// 使ったペンを元の状態に戻しておく
}

void Cx23RFPCDlg::graphCallback() {
	while (graphFlag) {
#ifdef _DEBUG
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif // _DEBUG
		drawGraph();
	}
}
//----------------------------------------------------------------------------//
//-----------------------------------Text-------------------------------------//
void Cx23RFPCDlg::drawText()
{
	CString tmp;
	if (evaluationFlag) {
		tmp.Format(_T("%.3f"), evaluation->getAS());
		msgED2.SetWindowTextW(tmp);
		tmp.Format(_T("%.3f"), evaluation->getSTL());
		msgED3.SetWindowTextW(tmp);
	}
	tmp.Format(_T(
		"W:\t%.3f\r\n\
AS:\t%.3f\r\n\
AATL:\t%.3f\r\n\
wx:\t%.3f\r\n\
wy:\t%.3f\r\n\
wz:\t%.3f\r\n\
STLx:\t%.3f\r\n\
STLy:\t%.3f\r\n\
STLz:\t%.3f\r\n\
Time:\t%.3f\r\n"
),
		data_container.getW(),
		data_container.getAS(),
		data_container.getAATL(),
		data_container.getVecW().x,
		data_container.getVecW().y,
		data_container.getVecW().z,
		data_container.getSTL().x,
		data_container.getSTL().y,
		data_container.getSTL().z,
		data_container.getTime()
	);
	msgED4.SetWindowTextW(tmp);
}

void Cx23RFPCDlg::textCallback()
{
	while (textFlag) {
#ifdef _DEBUG
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif // _DEBUG
		drawText();
	}
}

void Cx23RFPCDlg::setTextStyle()
{
	CFont* curFont;							// Prepare Pointer of CFont
	curFont = msgED2.GetFont();				// Get Current FOnt from Edit Control
	LOGFONTW mylf;							// Prepare variable for Multi Byte String
	curFont->GetLogFont(&mylf);				// Reflect Current FOnt to nylf
	mylf.lfHeight = 40;						// Change Font height
	mylf.lfWidth = 20;						// Change Font width 
	m_newFont = new CFont;					// Store Adress in Member variable that handling font
	m_newFont->CreateFontIndirectW(&mylf);	// Store Logical Font in m_newFont
	msgED2.SetFont(m_newFont);				// Set Font to Controller variablel
	msgED3.SetFont(m_newFont);
	msgED5.SetFont(m_newFont);
	msgED6.SetFont(m_newFont);
	curFont->Detach();
}
//----------------------------------------------------------------------------//
//----------------------------------Evaluation-------------------------------//
void  Cx23RFPCDlg::evaluationCallback() {
	while (evaluationFlag){
#ifdef _DEBUG
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif // _DEBUG
		evaluation->updateAS(data_container.getAS());
		evaluation->updateSTL(data_container.getSTL().z);
	}
}
//----------------------------------------------------------------------------//



