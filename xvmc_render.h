#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#include <X11/extensions/XvMClib.h>


//the surface should be shown, video driver manipulate this
#define MP_XVMC_STATE_DISPLAY_PENDING 1
//the surface is needed for prediction, codec manipulate this
#define MP_XVMC_STATE_PREDICTION 2
//                     1337    IDCT MCo
#define MP_XVMC_RENDER_MAGIC 0x1DC711C0

typedef   struct{
//these are not changed by decoder!
  int  magic;

  short * data_blocks;
  XvMCMacroBlock * mv_blocks;
  int total_number_of_mv_blocks;
  int total_number_of_data_blocks;
  int mc_type;//XVMC_MPEG1/2/4,XVMC_H263 without XVMC_IDCT
  int idct;//does we use IDCT acceleration?
  int chroma_format;//420,422,444
  int unsigned_intra;//+-128 for intra pictures after clip
  int reserved1[13];//future extenstions (e.g. gmc,qpel)
  XvMCSurface* p_surface;//pointer to rendered surface, never changed

//these are changed by decoder
//used by XvMCRenderSurface function
  XvMCSurface* p_past_surface;//pointer to the past surface
  XvMCSurface* p_future_surface;//pointer to  the future prediction surface

  unsigned int picture_structure;//top/bottom fields or frame  !
  unsigned int flags;//XVMC_SECOND_FIELD - 1'st or 2'd field in the sequence
  unsigned int display_flags; //1,2 or 1+2 fields for XvMCPutSurface, 

//these are internal communication one
  int state;//0-free,1 Waiting to Display,2 Waiting for prediction
  int start_mv_blocks_num;//offset in the array for the current slice,updated by vo
  int filled_mv_blocks_num;//processed mv block in this slice,change by decoder
  
  int next_free_data_block_num;//used in add_mv_block, pointer to next free block

} xvmc_render_state_t;
