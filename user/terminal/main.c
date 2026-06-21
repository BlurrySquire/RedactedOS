#include "syscalls/syscalls.h"
#include "draw/textdraw.h"
#include "shell/shell.h"
#include "shell/sheldon/sheldon.h"
#include "environment/env_types.h"
#include "data/serialize/binary_serial.h"
#include "kbd_helper.h"
#include "memory/memory.h"

text_format input_format = {};

#define INPUT_MARGIN 10
#define INPUT_HEIGHT (fb_line_height(input_format.scale)+(INPUT_MARGIN*2))

gpu_point scroll = {};

bool headless = false;
draw_text_op operation = draw_text_render;

bool cursor_on = false;
u64 cursor_blink_time = 500;

text_format current_formatting = {};
u32 bg_color;

buffer contents = {};
buffer input_buf = {};

draw_ctx ctx = {};

gpu_point cursor = {};
gpu_size out_size = {};

gpu_rect screen_rect = {};

shell_handle* main_shell;

range_t rerender_range = { };

void flush(shell_handle *handle);

void append(char *fmt, ...){
    __attribute__((aligned(16))) va_list args;
    va_start(args, fmt); 
    rerender_range = (range_t){ contents.cursor, 0 };
    size_t size = buffer_write_va(&contents, fmt, args);
    va_end(args);
    rerender_range.size = size;
    // scroll.y = 100;
    // if (scroll.y) {
    //     print("Scrolled %i",scroll.y);
    //     out_size = fb_draw_text(&ctx, slice_from_buffer(&contents), screen_rect, scroll, current_formatting, (text_format_arr){});
    //     // if ((i32)out_size.height - (i32)ctx.height >= 0) scroll.y += (i32)out_size.height - ctx.height;
    //     return;
    // }
    // print("Range is {%i,%i}",rerender_range.start,rerender_range.size);
    
    // if ((i32)out_size.height - (i32)ctx.height >= 0) scroll.y = -((i32)out_size.height - ctx.height);
    flush(main_shell);
}

shell_handle* make_default_shell(shell_bindings bindings){
    return create_sheldon(bindings, 0);
}

void put_char(shell_handle *handle, char c){
    // print("New char %c %x %x %i",c,handle,main_shell,contents.cursor);
    if (!c || (main_shell && main_shell != handle)) return;
    if (headless)
        serial_transmit(c);
    else {
        rerender_range.size += buffer_write_lim(&contents, &c, 1);
    }
}

void clear(shell_handle *handle){
    if (main_shell && main_shell != handle) return;
    buffer_wipe(&contents);
    fb_clear(&ctx, current_formatting.background);
}

void flush(shell_handle *handle){
    // print("Flush %v {%i,%i}",slice_from_buffer(&contents),rerender_range.start,rerender_range.size);
    if (main_shell && main_shell != handle) return;
    fb_continuous_draw_text(&ctx, operation, &cursor, slice_from_buffer(&contents), &rerender_range, screen_rect, &out_size, scroll, current_formatting, (text_format_arr){});
    ctx.full_redraw = true;
    commit_draw_ctx(&ctx);
    rerender_range = (range_t){ .start = contents.cursor };
    operation = draw_text_render;
}

void refresh_input(){
    fb_fill_rect(&ctx, 0, ctx.height-INPUT_HEIGHT, ctx.width, INPUT_HEIGHT, input_format.background);
    gpu_size offset = fb_draw_slice(&ctx, SLICE("> "), 0, ctx.height-INPUT_HEIGHT+INPUT_MARGIN, input_format.scale, input_format.foreground);
    fb_draw_slice(&ctx, slice_from_buffer(&input_buf), offset.width, ctx.height-INPUT_HEIGHT+INPUT_MARGIN, input_format.scale, input_format.foreground);
    u32 char_width = fb_char_width(input_format.scale);
    if (cursor_on)
        fb_fill_rect(&ctx, offset.width + (char_width * input_buf.cursor), ctx.height-INPUT_HEIGHT+INPUT_MARGIN, char_width, INPUT_HEIGHT-(INPUT_MARGIN*2), input_format.foreground);
    commit_draw_ctx(&ctx);
}

void bell(shell_handle *handle){
    if (main_shell && main_shell != handle) return;
    print("DING");
}

void ascii_cmd(shell_handle *handle, char cmd, u16 proc_id){
    if (main_shell && main_shell != handle) return;
    print("[TERM implementation error] ascii commands not supported");
}

void console_ctrl(shell_handle *handle, console_ctrls ctrl){
    if (main_shell && main_shell != handle) return;
    print("[TERM implementation error] console control not supported");
}

void emit_data(structdef field, sizedptr data, bool is_allocated){
    // if (main_shell && main_shell != handle) return;
    print("[TERM implementation error] structured data not supported");
}

shell_bindings terminal_bindings = (shell_bindings){
    .console_output = put_char,
    .console_flush = flush,
    .console_clean = clear,
    .console_bell = bell,
    .console_ascii_cmd = ascii_cmd,
    .console_control = console_ctrl
};

shell_handle* create_shell(){
    shell_handle *handle = make_default_shell(terminal_bindings);
    if (!handle) return 0;
    return handle;
}

bool erase(bool forward){
    if (forward && input_buf.cursor >= input_buf.buffer_size) return false;
    if (!buffer_delete(&input_buf, input_buf.cursor + forward, 1)) return false;

    refresh_input();
    return true;
}

bool run_command(){

    bool success = false;

    append("\r\n");
    
    if (run_cmd(main_shell, slice_from_buffer(&input_buf))) success = true;

    buffer_wipe(&input_buf);
    return success;
}

bool move_buf_cursor(i64 amount){
    if (!amount) return false;
    uptr old_in_c = input_buf.cursor;
    if (buffer_seek(&input_buf, amount, false) == old_in_c) return false;
    refresh_input();
    return true;
}

bool handle_input(){
    kbd_event event;
    if (!read_event(&event)) return false;
    if (event.type == KEY_RELEASE) return true;
    if (handle_modifier(&event)) return true;

    char key = event.key;
    char readable = hid_to_char((uint8_t)key, current_modifier, special_key);

    if (key == KEY_ENTER || key == KEY_KPENTER) return run_command();

    if (key == KEY_BACKSPACE) return erase(false);
    if (key == KEY_DELETE) return erase(true);
    //Up & Down
    
    if (key == KEY_LEFT) return move_buf_cursor(-1);
    if (key == KEY_RIGHT) return move_buf_cursor(1);
    
    if (!readable) return false;
    
    if (!input_buf.buffer) input_buf = buffer_create(0x100, buffer_can_grow);

    buffer_write_lim(&input_buf, &readable, 1);
    refresh_input();

    flush(main_shell);
    
    return true;
    
}

void toggle_cursor(){
    cursor_on = !cursor_on;
    refresh_input();
}

int main(){    
    request_app_ctx(&ctx);

    current_formatting.scale = 3;

    screen_rect = (gpu_rect){ .point = {}, .size = { ctx.width, ctx.height-INPUT_HEIGHT }};

    current_formatting.wrap = wrap_word;

    contents = buffer_create(0x100, buffer_can_grow);

    u32 color_buf[2] = {};
    sreadf("/theme", &color_buf, sizeof(uint64_t));
    if ((color_buf[0] & 0xFF000000) == 0) color_buf[0] |= 0xFF000000;
    if ((color_buf[1] & 0xFF000000) == 0) color_buf[1] |= 0xFF000000;
    current_formatting.background = color_buf[0];
    current_formatting.foreground = color_buf[1];

    memcpy(&input_format, &current_formatting, sizeof(text_format));

    fb_clear(&ctx, current_formatting.background);
    // int i = 0;

    main_shell = create_shell();

    current_shell = main_shell;

    u64 cursor_current = 0;

    u64 last_time = get_time();
    refresh_input();
    while (true){
        // append("Bonjour %.3i \t",i++);
        u64 current_time = get_time();
        cursor_current += (current_time-last_time);
        if (cursor_current >= cursor_blink_time){
            toggle_cursor();
            cursor_current = 0;
        }
        last_time = current_time;
        handle_input();
        // msleep(1);
    }
    
    return 0;
}