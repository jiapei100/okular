//
// Class: dviWindow
//
// Previewer for TeX DVI files.
//

#include <stdlib.h>
#include <unistd.h>

#include <qbitmap.h> 
#include <qkeycode.h>
#include <qpaintdevice.h>
#include <qfileinfo.h>

#include <kapp.h>
#include <kmessagebox.h>
#include <kdebug.h>
#include <klocale.h>

#include "dviwin.h"
#include "prefs.h"

//------ some definitions from xdvi ----------


#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

struct	WindowRec {
	Window		win;
	int		shrinkfactor;
	int		base_x, base_y;
	unsigned int	width, height;
	int	min_x, max_x, min_y, max_y;
};

extern	struct WindowRec mane, alt, currwin;

#include "c-openmx.h" // for OPEN_MAX

	float	_gamma;
	int	_pixels_per_inch;
	_Xconst char	*_paper;
	Pixel	_fore_Pixel;
	Pixel	_back_Pixel;
	Boolean	_postscript;
	Boolean	useGS;
	Boolean	_use_grey;

extern char *           prog;
extern char *	        dvi_name;
extern FILE *		dvi_file;
extern int 		n_files_left;
extern int 		min_x;
extern int 		min_y;
extern int 		max_x;
extern int 		max_y;
extern int		offset_x, offset_y;
extern unsigned int	unshrunk_paper_w, unshrunk_paper_h;
extern unsigned int	unshrunk_page_w, unshrunk_page_h;
extern unsigned int	page_w, page_h;
extern int 		current_page;
extern int 		total_pages;
extern Display *	DISP;
extern Screen  *	SCRN;
Window mainwin;
int			useAlpha;

extern "C" void 	draw_page(void);
extern "C" void 	kpse_set_progname(const char*);
extern "C" Boolean 	check_dvi_file();
extern "C" void 	reset_fonts();
extern "C" void 	init_page();
extern "C" void 	init_pix(Boolean warn);
extern "C" void 	psp_destroy();
extern "C" void 	psp_toggle();
extern "C" void 	psp_interrupt();
extern "C" {
#undef PACKAGE // defined by both c-auto.h and config.h
#undef VERSION
#include <kpathsea/c-auto.h>
#include <kpathsea/paths.h>
#include <kpathsea/proginit.h>
#include <kpathsea/tex-file.h>
#include <kpathsea/tex-glyph.h>
}

//------ next the drawing functions called from C-code (dvi_draw.c) ----

QPainter *dcp;

extern "C" void qtPutRule(int x, int y, int w, int h)
{
	dcp->fillRect( x, y, w, h, Qt::black );
}

extern "C" void qtPutBorder(int x, int y, int w, int h)
{
	dcp->drawRect( x, y, w, h );
}

extern "C" void qtPutBitmap( int x, int y, int w, int h, uchar *bits )
{
	QBitmap bm( w, h, bits, TRUE );
	dcp->drawPixmap( x, y, bm );
}

extern "C" void qt_processEvents()
{
	qApp->processEvents();
}

//------ now comes the dviWindow class implementation ----------

dviWindow::dviWindow( int bdpi, const char *mfm, const char *ppr,
                      int mkpk, QWidget *parent, const char *name )
	: QScrollView( parent, name )
{
	ChangesPossible = 1;
	FontPath = QString::null;
	viewport()->setBackgroundColor( white );
	setFocusPolicy(QWidget::StrongFocus);
	setFocus();

	timer = new QTimer( this );
	connect( timer, SIGNAL(timeout()),
		 this, SLOT(timerEvent()) );
	checkinterval = 1000;

        connect(this, SIGNAL(contentsMoving(int, int)),
                this, SLOT(contentsMoving(int, int)));
        
	// initialize the dvi machinery

	setResolution( bdpi );
	setMakePK( mkpk );
	setMetafontMode( mfm );
	setPaper( ppr );

	DISP = x11Display();
	mainwin = handle();
	currwin.shrinkfactor = 6;
	mane = currwin;
	SCRN = DefaultScreenOfDisplay(DISP);
	_fore_Pixel = BlackPixelOfScreen(SCRN);
	_back_Pixel = WhitePixelOfScreen(SCRN);
	useGS = 1;
	_postscript = 0;
	_use_grey = 1;
	pixmap = NULL;
	init_pix(FALSE);
}

dviWindow::~dviWindow()
{
	psp_destroy();
}

void dviWindow::setChecking( int time )
{
	checkinterval = time;
	if ( timer->isActive() )
		timer->changeInterval( time );
}

int dviWindow::checking()
{
	return checkinterval;
}

void dviWindow::setShowScrollbars( int flag )
{
        if( flag ) { 
	        setVScrollBarMode(Auto);
	        setHScrollBarMode(Auto);                
	}
        else {
	        setVScrollBarMode(AlwaysOff);
	        setHScrollBarMode(AlwaysOff);                          
        }
}

int dviWindow::showScrollbars()
{
	return  (vScrollBarMode() == Auto);
}

void dviWindow::setShowPS( int flag )
{
	if ( _postscript == flag )
		return;
	_postscript = flag;
	psp_toggle();
	drawPage();
}

int dviWindow::showPS()
{
	return _postscript;
}

void dviWindow::setAntiAlias( int flag )
{
	if ( !useAlpha == !flag )
		return;
	useAlpha = flag;
	psp_destroy();
	drawPage();
}

int dviWindow::antiAlias()
{
	return useAlpha;
}

void dviWindow::setMakePK( int flag )
{
	if (!ChangesPossible)
		KMessageBox::sorry( this,
			i18n("The change in font generation will be effective\n"
			"only after you start kdvi again!") );
	makepk = flag;
}

int dviWindow::makePK()
{
	return makepk;	
}
	
void dviWindow::setFontPath( const char *s )
{
	if (!ChangesPossible)
		KMessageBox::sorry( this,
			i18n("The change in font path will be effective\n"
			"only after you start kdvi again!"));
	FontPath = s;
}

const char * dviWindow::fontPath()
{
	return FontPath;
}

void dviWindow::setMetafontMode( const char *mfm )
{
	if (!ChangesPossible)
		KMessageBox::sorry( this,
			i18n("The change in Metafont mode will be effective\n"
			"only after you start kdvi again!") );
	MetafontMode = mfm;
}

const char * dviWindow::metafontMode()
{
	return MetafontMode;
}

void dviWindow::setPaper( const char *paper )
{
	if ( !paper )
		return;
	paper_type = paper;
	_paper = paper_type;
	float w, h;
	if (!kdviprefs::paperSizes( paper, w, h ))
	{
		kdebug(KDEBUG_WARN, 4300, "Unknown paper type!");
		// A4 paper is used as default, if paper is unknown
		w = 21.0/2.54;
		h = 29.7/2.54;
	}
	unshrunk_paper_w = int( w * basedpi + 0.5 );
	unshrunk_paper_h = int( h * basedpi + 0.5 ); 
}

const char * dviWindow::paper()
{
	return paper_type;
}

void dviWindow::setResolution( int bdpi )
{
	if (!ChangesPossible)
		KMessageBox::sorry( this,
			i18n("The change in resolution will be effective\n"
			"only after you start kdvi again!") );
	basedpi = bdpi;
	_pixels_per_inch = bdpi;
	offset_x = offset_y = bdpi;
}

int dviWindow::resolution()
{
	return basedpi;
}

void dviWindow::setGamma( float gamma )
{
	if (!ChangesPossible)
	{
		KMessageBox::sorry( this,
			i18n("The change in gamma will be effective\n"
			"only after you start kdvi again!"), i18n( "OK" ) );
		return;	// Qt will kill us otherwise ??
	}
	_gamma = gamma;
	init_pix(FALSE);
}

float dviWindow::gamma()
{
	return _gamma;
}


//------ reimplement virtual event handlers -------------

void dviWindow::viewportMousePressEvent ( QMouseEvent *e)
{
        if (!(e->button()&LeftButton))
		return;
	mouse = e->pos();
        emit setPoint( viewportToContents(mouse) );
}

void dviWindow::viewportMouseMoveEvent ( QMouseEvent *e)
{
	if (!(e->state()&LeftButton))
		return;
        QPoint diff = mouse - e->pos();
	mouse = e->pos();
        scrollBy(diff.x(), diff.y());
}

void dviWindow::keyPressEvent ( QKeyEvent *e)
{
	const int slowScrollSpeed = 1;
	const int fastScrollSpeed = 15;
	int speed = e->state() & ControlButton ?
			slowScrollSpeed : fastScrollSpeed;

	switch( e->key() ) {
	case Key_Next:	nextPage();				break;
	case Key_Prior:	prevPage();				break;
	case Key_Space:	goForward();				break;
	case Key_Plus:	prevShrink();				break;
	case Key_Minus:	nextShrink();				break;
        case Key_Down:	scrollBy(0,speed);              	break;
	case Key_Up:	scrollBy(0,-speed);             	break;
	case Key_Right:	scrollBy(speed,0);              	break;
	case Key_Left:	scrollBy(-speed,0);                     break;
	case Key_Home:	
		if (e->state() == ControlButton)
			firstPage();
		else
			setContentsPos(0,0);
		break;
	case Key_End:
		if (e->state() == ControlButton)
			lastPage();
		else
			setContentsPos(0, contentsHeight()-visibleHeight());
		break;
	default:	e->ignore();				break;
	}
}

void dviWindow::initDVI()
{
        prog = const_cast<char*>("kdvi");
	n_files_left = OPEN_MAX;
	kpse_set_progname ("xdvi");
	kpse_init_prog ("XDVI", basedpi, MetafontMode.data(), "cmr10");
	kpse_set_program_enabled(kpse_any_glyph_format,
				 makepk, kpse_src_client_cnf);
	kpse_format_info[kpse_pk_format].override_path
		= kpse_format_info[kpse_gf_format].override_path
		= kpse_format_info[kpse_any_glyph_format].override_path
		= kpse_format_info[kpse_tfm_format].override_path
		= FontPath.ascii();
	ChangesPossible = 0;
}

#include <setjmp.h>
extern	jmp_buf	dvi_env;	/* mechanism to communicate dvi file errors */
extern	char *dvi_oops_msg;
extern	time_t dvi_time;

//------ this function calls the dvi interpreter ----------

void dviWindow::drawDVI()
{
	psp_interrupt();
	if (filename.isEmpty())	// must call setFile first
		return;
	if (!dvi_name)
	{			//  dvi file not initialized yet
		QApplication::setOverrideCursor( waitCursor );
		dvi_name = const_cast<char*>(filename.ascii());

		dvi_file = NULL;
		if (setjmp(dvi_env))
		{	// dvi_oops called
			dvi_time = 0; // force init_dvi_file
			QApplication::restoreOverrideCursor();
			KMessageBox::error( this,
				i18n("What's this? DVI problem!\n")
					+ dvi_oops_msg);
			return;
		}
		if ( !check_dvi_file() )
			emit fileChanged();

		QApplication::restoreOverrideCursor();
		gotoPage(1);
		changePageSize();
                emit viewSizeChanged( QSize( visibleWidth(),visibleHeight() ));
		timer->start( 1000 );
		return;
	}
	min_x = 0;
	min_y = 0;
	max_x = page_w;
	max_y = page_h;

	if ( !pixmap )	return;
	if ( !pixmap->paintingActive() )
	{
		QPainter paint;
		paint.begin( pixmap );
		QApplication::setOverrideCursor( waitCursor );
		dcp = &paint;
		if (setjmp(dvi_env))
		{	// dvi_oops called
			dvi_time = 0; // force init_dvi_file
			QApplication::restoreOverrideCursor();
			paint.end();
			KMessageBox::error( this,
				i18n("What's this? DVI problem!\n") 
					+ dvi_oops_msg);
			return;
		}
		else
		{
			if ( !check_dvi_file() )
				emit fileChanged();
			pixmap->fill( white );
			draw_page();
		}
		QApplication::restoreOverrideCursor();
		paint.end();
	}
}

void dviWindow::drawPage()
{
  drawDVI();
  repaintContents(contentsX(), contentsY(), 
                  visibleWidth(), visibleHeight(), FALSE);
}

bool dviWindow::changedDVI()
{
	return changetime != QFileInfo(filename).lastModified();
}

bool dviWindow::correctDVI()
{
	QFile f(filename);
	if (!f.open(IO_ReadOnly))
		return FALSE;
	int n = f.size();
	if ( n < 134 )	// Too short for a dvi file
		return FALSE;
	f.at( n-4 );
	char test[4];
	unsigned char trailer[4] = { 0xdf,0xdf,0xdf,0xdf };
	if ( f.readBlock( test, 4 )<4 || strncmp( test, (char *) trailer, 4 ) )
		return FALSE;
	// We suppose now that the dvi file is complete	and OK
	return TRUE;
}
void dviWindow::timerEvent()
{
	static int changing = 0;

	if ( !changedDVI() )
		return;
	if ( !changing )
		emit statusChange( i18n("File status changed.") );
	changing = 1;
	if ( !correctDVI() )
		return;
	changing = 0;
	emit statusChange( i18n("File reloaded.") );
	changetime = QFileInfo(filename).lastModified();
	drawPage();
	emit fileChanged();
}

void dviWindow::changePageSize()
{
	if ( pixmap && pixmap->paintingActive() )
		return;
	psp_destroy();
	if (pixmap)
		delete pixmap;
	pixmap = new QPixmap( (int)page_w, (int)page_h );
	emit pageSizeChanged( QSize( page_w, page_h ) );
        resizeContents( page_w, page_h );
        
	currwin.win = mane.win = pixmap->handle();
	
        drawPage();
}

//------ setup the dvi interpreter (should do more here ?) ----------

void dviWindow::setFile( const char *fname )
{
        if (ChangesPossible){
            initDVI();
        }
        filename = fname;
        dvi_name = 0;
        changetime = QFileInfo(filename).lastModified();
        drawPage();
}

//------ following member functions are in the public interface ----------

QPoint dviWindow::currentPos()
{
	return QPoint(contentsX(), contentsY());
}

void dviWindow::scroll( QPoint to )
{
	setContentsPos(to.x(), to.y());
}

QSize dviWindow::viewSize()
{
	return QSize( visibleWidth(), visibleHeight() );
}

QSize dviWindow::pageSize()
{
	return QSize( page_w, page_h );
}

//------ handling pages ----------

void dviWindow::goForward()
{
  if(contentsY() >= contentsHeight()-visibleHeight()) {
    nextPage();
    setContentsPos(0, 0);
  }
  else
    scrollBy(0, 2*visibleWidth()/3);
}

void dviWindow::prevPage()
{
	gotoPage( page()-1 );
}

void dviWindow::nextPage()
{
	gotoPage( page()+1 );
}

void dviWindow::gotoPage(int new_page)
{
	if (new_page<1)
		new_page = 1;
	if (new_page>total_pages)
		new_page = total_pages;
	if (new_page-1==current_page)
		return;
	current_page = new_page-1;
	emit currentPage(new_page);
	drawPage();
}

void dviWindow::firstPage()
{
	gotoPage(1);
}

void dviWindow::lastPage()
{
	gotoPage(totalPages());
}

int dviWindow::page()
{
	return current_page+1;
}

int dviWindow::totalPages()
{
	return total_pages;
}

//------ handling shrink factor ----------

int dviWindow::shrink()
{
	return currwin.shrinkfactor;
}

void dviWindow::prevShrink()
{
	setShrink( shrink() - 1 );
}

void dviWindow::nextShrink()
{
	setShrink( shrink() + 1 );
}

void dviWindow::setShrink(int s)
{
	int olds = shrink();

	if (s<=0 || s>basedpi/20 || olds == s)
		return;
	mane.shrinkfactor = currwin.shrinkfactor = s;
	init_page();
	init_pix(FALSE);
	reset_fonts();
	changePageSize();
	emit shrinkChanged( shrink() );
}

void   dviWindow::resizeEvent(QResizeEvent *e)
{
       QScrollView::resizeEvent(e);
       emit viewSizeChanged( QSize( visibleWidth(),visibleHeight() ));
}	

void   dviWindow::contentsMoving( int x, int y ) 
{
  emit currentPosChanged( QPoint(x, y) );
}
  

void   dviWindow::drawContents(QPainter *p, 
                               int clipx, int clipy, 
                               int clipw, int cliph ) 
{
  if ( pixmap )
    p->drawPixmap(clipx, clipy, *pixmap, clipx, clipy, clipw, cliph);
}
