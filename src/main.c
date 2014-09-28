#include <pebble.h>
  
static Window *s_main_window;
static TextLayer *s_time_layer;
static Layer *minute_display_layer;
static InverterLayer *inverter_layer;

// we use our first version of the persistent storage
#define STORAGE_VERSION 1

// PebbleKit JS???
#define KEY_INVERT 0

// persistent memory layout
#define STORE_BASE 123400000
#define STORE_VERSION 1 + STORE_BASE
#define STORE_INVERT  2 + STORE_BASE

static bool InvertWatchface = false;
static bool CurrState_Inverted = false;
  
const GPathInfo MINUTE_SEGMENT_STRAIGTH_PATH_POINTS = {
  4,
  (GPoint []) {
    { 30,  0},
    {114,  0},
    {104, 30},
    { 40, 30},
  }
};

const GPathInfo MINUTE_SEGMENT_DIAGONAL_PATH_POINTS = {
  5,
  (GPoint []) {
    {114,  0},
    {144,  0},
    {144, 42},
    {114, 52},
    {104, 30},
  }
};

static GPath *s_minute_segment_straight_path;
static GPath *s_minute_segment_diagonal_path;


static void minute_display_layer_update_callback(Layer *layer, GContext* ctx)
{
  GPath *minute_segment_path;
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // scale by 4 to avoid floating point math
  int segment = (((t->tm_min * 4) + 15) / 30) % 8;
  
  //segment = 7; // debug
  
  if ((segment % 2) == 0)
    minute_segment_path = s_minute_segment_straight_path;
  else
    minute_segment_path = s_minute_segment_diagonal_path;

  // always rotate around (0,0)
  gpath_move_to(minute_segment_path, GPointZero);
  const unsigned int angle = (segment / 2) * 90;
  gpath_rotate_to(minute_segment_path, (TRIG_MAX_ANGLE / 360) * angle);  

  switch (segment)
  {
    case 2:
      gpath_move_to(minute_segment_path, GPoint (144, 12));
      break;
    case 3:
      gpath_move_to(minute_segment_path, GPoint (144, 24));
      break;
    case 4:
      gpath_move_to(minute_segment_path, GPoint (144, 168));
      break;
    case 5:
      gpath_move_to(minute_segment_path, GPoint (142, 168));
      break;
    case 6:
      gpath_move_to(minute_segment_path, GPoint (-1, 156));
      break;
    case 7:
      gpath_move_to(minute_segment_path, GPoint (-1, 143));
      break;
  }
  
  graphics_context_set_fill_color(ctx, GColorBlack);

  gpath_draw_filled(ctx, minute_segment_path);
}

static void update_time()
{
  static int oldHour = -1;
  // Create a long-lived buffer
  static char buffer[] = "00";

  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  
  if (tick_time->tm_min > 56)
  {
    tick_time->tm_hour = tick_time->tm_hour + 1;
  }

  if (oldHour == tick_time->tm_hour) return; // don't update, if nothing changes
  oldHour = tick_time->tm_hour;

  // Write the current hours into the buffer
  if(clock_is_24h_style() == true) {
    // Use 24 hour format
    strftime(buffer, sizeof("00"), "%H", tick_time);
  } else {
    // Use 12 hour format
    strftime(buffer, sizeof("00"), "%I", tick_time);
  }

  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed)
{
  static int oldSegment = -1;
  
  int segment = (((tick_time->tm_min * 4) + 15) / 30) % 8;
  
  // don't update, if nothing changed
  if (oldSegment != segment)
  {
    oldSegment = segment;
    layer_mark_dirty(minute_display_layer);
  } 
  
  update_time();
}

static void main_window_load(Window *window)
{
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  // Create time TextLayer
  s_time_layer = text_layer_create(GRect(0, 44, 144, 80));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);

  // Improve the layout to be more like a watchface
  //text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_font(s_time_layer,
      fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JURA_SUBSET_68)));
  
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
  layer_destroy(minute_display_layer);
  
  if (CurrState_Inverted == true)
  {
    inverter_layer_destroy(inverter_layer);
  }
  gpath_destroy (s_minute_segment_straight_path);
  gpath_destroy (s_minute_segment_diagonal_path);
}

// set the current inversion
void SetInversion (const bool _newInversionState)
{
  if (CurrState_Inverted != _newInversionState)
  {
    if (CurrState_Inverted == true)
    {
      inverter_layer_destroy(inverter_layer);
    }
    else
    {
      Layer *window_layer = window_get_root_layer(s_main_window);
      GRect bounds = layer_get_bounds(window_layer);
      
      inverter_layer = inverter_layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
      layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));  
    }
    CurrState_Inverted = _newInversionState;
  }
}

// receive JS framework messages (from the app configuration)
static void in_recv_handler(DictionaryIterator *iterator, void *context)
{
  //Get Tuple
  Tuple *t = dict_read_first(iterator);
  if(t)
  {
    switch(t->key)
    {
    case KEY_INVERT:
      //It's the KEY_INVERT key
      if(strcmp(t->value->cstring, "on") == 0)
      {
        //Set and save as inverted
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Conf: Inverted: on");
 
        InvertWatchface =  true;
        persist_write_bool(STORE_INVERT, true);
      }
      else if(strcmp(t->value->cstring, "off") == 0)
      {
        //Set and save as not inverted
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Conf: Inverted: off");
 
        InvertWatchface =  false;
        persist_write_bool(STORE_INVERT, false);
      }
      break;
    }
  }

  // update to current configuration
  SetInversion (InvertWatchface);
}

static void init()
{
  if(!persist_exists(STORE_VERSION) ||
      STORAGE_VERSION != persist_read_int(STORE_VERSION))
  {
    int ret;
    
    ret = persist_write_int(STORE_VERSION, STORAGE_VERSION);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "STORE_VERSION: %d", ret);

    ret = persist_write_bool(STORE_INVERT, true);
    InvertWatchface = true;
  }
  else
  {
    InvertWatchface = persist_read_bool(STORE_INVERT);
    if (InvertWatchface == true)
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Conf: Inverted: true");
    else
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Conf: Inverted: off");
  }
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // register for app configuration messages
  app_message_register_inbox_received((AppMessageInboxReceived) in_recv_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  
  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(window_layer);

  // Init the layer for the minute display
  minute_display_layer = layer_create(bounds);
  layer_set_update_proc(minute_display_layer, minute_display_layer_update_callback);
  layer_add_child(window_layer, minute_display_layer);
  
  SetInversion (InvertWatchface);

  // Init the minute segment paths
  s_minute_segment_straight_path = gpath_create(&MINUTE_SEGMENT_STRAIGTH_PATH_POINTS);
  s_minute_segment_diagonal_path = gpath_create(&MINUTE_SEGMENT_DIAGONAL_PATH_POINTS);
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);  
  
  // Make sure the time is displayed from the start
  update_time();
}

static void deinit() {
  tick_timer_service_unsubscribe();
  // deregister JS messaging
  app_message_deregister_callbacks ();
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
