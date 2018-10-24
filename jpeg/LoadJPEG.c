#include <afx.h>
#include <afxtempl.h>
#include <setjmp.h>

/* this is not a core library module, so it doesn't define JPEG_INTERNALS */

#define XMD_H

extern "C"
{
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"

/*
#include "jpeg2\jinclude.h"
#include "jpeg2\jpeglib.h"
#include "jpeg2\jerror.h"
*/
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

//
//
//

METHODDEF(void) my_error_exit (j_common_ptr cinfo);

//
//	to handle fatal errors.
//	the original JPEG code will just exit(0). can't really
//	do that in Windows....
//

METHODDEF(void) my_error_exit (j_common_ptr cinfo)
{
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr myerr = (my_error_ptr) cinfo->err;

	char buffer[JMSG_LENGTH_MAX];

	/* Create the message */
	(*cinfo->err->format_message) (cinfo, buffer);

	/* Always display the message. */
	TRACE("JPEG Fatal Error: %s\n",buffer);


	/* Return control to the setjmp point */
	longjmp(myerr->setjmp_buffer, 1);
}

/////////////////////////////////////////////////////////////////////////////

/* Expanded data source object for CFile input */

typedef struct {
  struct jpeg_source_mgr pub;	/* public fields */

  CFile * infile;		/* source stream */
  JOCTET * buffer;		/* start of buffer */
  boolean start_of_file;	/* have we gotten any data yet? */
} my_source_mgr;

typedef my_source_mgr * my_src_ptr;

#define INPUT_BUF_SIZE  4096	/* choose an efficiently fread'able size */


/*
 * Initialize source --- called by jpeg_read_header
 * before any data is actually read.
 */

METHODDEF(void)
init_source (j_decompress_ptr cinfo)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  /* We reset the empty-input-file flag for each image,
   * but we don't clear the input buffer.
   * This is correct behavior for reading a series of images from one source.
   */
  src->start_of_file = TRUE;
}


/*
 * Fill the input buffer --- called whenever buffer is emptied.
 *
 * In typical applications, this should read fresh data into the buffer
 * (ignoring the current state of next_input_byte & bytes_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been reloaded.  It is not necessary to
 * fill the buffer entirely, only to obtain at least one more byte.
 *
 * There is no such thing as an EOF return.  If the end of the file has been
 * reached, the routine has a choice of ERREXIT() or inserting fake data into
 * the buffer.  In most cases, generating a warning message and inserting a
 * fake EOI marker is the best course of action --- this will allow the
 * decompressor to output however much of the image is there.  However,
 * the resulting error message is misleading if the real problem is an empty
 * input file, so we handle that case specially.
 *
 * In applications that need to be able to suspend compression due to input
 * not being available yet, a FALSE return indicates that no more data can be
 * obtained right now, but more may be forthcoming later.  In this situation,
 * the decompressor will return to its caller (with an indication of the
 * number of scanlines it has read, if any).  The application should resume
 * decompression after it has loaded more data into the input buffer.  Note
 * that there are substantial restrictions on the use of suspension --- see
 * the documentation.
 *
 * When suspending, the decompressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_input_byte & bytes_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point must be rescanned after resumption, so move it to
 * the front of the buffer rather than discarding it.
 */

METHODDEF(boolean)
fill_input_buffer (j_decompress_ptr cinfo)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;
  size_t nbytes;

  nbytes = src->infile->Read(src->buffer, INPUT_BUF_SIZE);

  if (nbytes <= 0) {
    if (src->start_of_file)	/* Treat empty input file as fatal error */
      ERREXIT(cinfo, JERR_INPUT_EMPTY);
    WARNMS(cinfo, JWRN_JPEG_EOF);
    /* Insert a fake EOI marker */
    src->buffer[0] = (JOCTET) 0xFF;
    src->buffer[1] = (JOCTET) JPEG_EOI;
    nbytes = 2;
  }

  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = nbytes;
  src->start_of_file = FALSE;

  return TRUE;
}


/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 * Writers of suspendable-input applications must note that skip_input_data
 * is not granted the right to give a suspension return.  If the skip extends
 * beyond the data currently in the buffer, the buffer can be marked empty so
 * that the next read will cause a fill_input_buffer call that can suspend.
 * Arranging for additional bytes to be discarded before reloading the input
 * buffer is the application writer's problem.
 */

METHODDEF(void)
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
  my_src_ptr src = (my_src_ptr) cinfo->src;

  /* Just a dumb implementation for now.  Could use fseek() except
   * it doesn't work on pipes.  Not clear that being smart is worth
   * any trouble anyway --- large skips are infrequent.
   */
  if (num_bytes > 0) {
    while (num_bytes > (long) src->pub.bytes_in_buffer) {
      num_bytes -= (long) src->pub.bytes_in_buffer;
      (void) fill_input_buffer(cinfo);
      /* note we assume that fill_input_buffer will never return FALSE,
       * so suspension need not be handled.
       */
    }
    src->pub.next_input_byte += (size_t) num_bytes;
    src->pub.bytes_in_buffer -= (size_t) num_bytes;
  }
}


/*
 * An additional method that can be provided by data source modules is the
 * resync_to_restart method for error recovery in the presence of RST markers.
 * For the moment, this source module just uses the default resync method
 * provided by the JPEG library.  That method assumes that no backtracking
 * is possible.
 */


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

METHODDEF(void)
term_source (j_decompress_ptr cinfo)
{
  /* no work necessary here */
}


/*
 * Prepare for input from a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing decompression.
 */

GLOBAL(void)
jpeg_CFile_src (j_decompress_ptr cinfo, CFile * infile)
{
  my_src_ptr src;

  /* The source object and input buffer are made permanent so that a series
   * of JPEG images can be read from the same file by calling jpeg_stdio_src
   * only before the first one.  (If we discarded the buffer at the end of
   * one image, we'd likely lose the start of the next one.)
   * This makes it unsafe to use this manager and a different source
   * manager serially with the same JPEG object.  Caveat programmer.
   */
  if (cinfo->src == NULL) {	/* first time for this JPEG object? */
    cinfo->src = (struct jpeg_source_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  SIZEOF(my_source_mgr));
    src = (my_src_ptr) cinfo->src;
    src->buffer = (JOCTET *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  INPUT_BUF_SIZE * SIZEOF(JOCTET));
  }

  src = (my_src_ptr) cinfo->src;
  src->pub.init_source = init_source;
  src->pub.fill_input_buffer = fill_input_buffer;
  src->pub.skip_input_data = skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->pub.term_source = term_source;
  src->infile = infile;
  src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
  src->pub.next_input_byte = NULL; /* until buffer loaded */
}

HBITMAP CreateHBitmap(int w, int h, LPVOID *lpBits)
{
	HBITMAP bitmap;
	BITMAPINFOHEADER BIH ;
	int iSize = sizeof(BITMAPINFOHEADER) ;
	memset(&BIH, 0, iSize);

	// Fill in the header info.
	BIH.biSize = iSize;
	BIH.biWidth = w;
	BIH.biHeight = h;
	BIH.biPlanes = 1;
	BIH.biBitCount = 24;
	BIH.biCompression = BI_RGB;
	HDC hDC = ::CreateCompatibleDC(NULL);
	bitmap = CreateDIBSection(hDC,
							(BITMAPINFO*)&BIH,
							DIB_RGB_COLORS,
							lpBits,
							NULL,
							0);
	::DeleteDC(hDC);
	return bitmap;
}

/////////////////////////////////////////////////////////////////////
extern "C" __declspec(dllexport) 
HBITMAP CreateBitmapFromJPGFile(LPCTSTR pFileName)
{
	CFile file;
	if( !file.Open( pFileName, CFile::modeRead, NULL ) )
	{
		return NULL;
	}
	jpeg_decompress_struct cinfo;  // IJPEG decoder state.
	my_error_mgr jerr;			  // Custom error manager.

	cinfo.err = jpeg_std_error (&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp(jerr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled an error.
		 * We need to clean up the JPEG object, close the input file, and return.
		 */

		jpeg_destroy_decompress(&cinfo);
		file.Close();
		return NULL;
	}

	jpeg_create_decompress (&cinfo);

	// Initialize custom data source.
	jpeg_CFile_src (&cinfo, &file);

	jpeg_read_header (&cinfo, TRUE);

	if (/*cinfo.out_color_space != JCS_GRAYSCALE && */cinfo.out_color_space != JCS_RGB) {
		jpeg_destroy_decompress(&cinfo);
		TRACE("LoadJPEG fail! out_color_space = %d\n", cinfo.out_color_space);
		file.Close();
		return NULL;
	}

	/*if (m_bFast)
	{
	  cinfo.do_fancy_upsampling = FALSE;
	}*/

	// Choose floating point DCT method.
	cinfo.dct_method = JDCT_FLOAT;

	int w = cinfo.image_width;
	int h = cinfo.image_height;

	jpeg_start_decompress (&cinfo);

	ASSERT(cinfo.out_color_components==3);

	BYTE *pDst = NULL;
	HBITMAP hBitmap = CreateHBitmap(w, h, (void**)&pDst);
	if(!hBitmap)
	{
		file.Close();
		return NULL;
	}

//	BYTE * pDst = NULL;
	int nStorageWidth = ((w * 24 + 31) & ~31) >> 3; //dword alignment
	JSAMPARRAY ppDst = &pDst;

	/* Here we use the library's state variable cinfo.output_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	*/
	while (cinfo.output_scanline < cinfo.output_height) {
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could ask for
		 * more than one scanline at a time if that's more convenient.
		 */
      jpeg_read_scanlines (&cinfo, ppDst, 1);

	  if(cinfo.out_color_components==3) {
		  //swap R & B
		  BYTE* p = pDst;
		  for(int i=0;i<w;i++) {
			  BYTE r = p[0];
			  p[0] = p[2];
			  p[2] = r;
			  p += 3;
		  }
	  }

	  pDst += nStorageWidth;
	}

	jpeg_finish_decompress (&cinfo);
	
	jpeg_destroy_decompress (&cinfo);

	file.Close();
	return hBitmap;
}

