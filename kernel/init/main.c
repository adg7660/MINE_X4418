#include <stdio.h>
#include <interrupt.h>
#include <serial.h>
#include <assert.h>
#include <printk.h>
#include <softirq.h>
#include <schedule.h>
#include <irqflags.h>
#include <core/initcall.h>
#include <framebuffer/framebuffer.h>
int main() {

	init_memory();
	slab_init();

	printf("schedule_init\n");
	schedule_init();

	/* Do initial event */
	do_init_event();

	/* Do all initial calls */
	do_initcalls();
	
	struct framebuffer_t * fb = search_framebuffer("fb-s5p4418.0");
	struct render_t * render = framebuffer_create_render(fb);
	int width = framebuffer_get_width(fb);
	int height = framebuffer_get_height(fb);
	int bpp = framebuffer_get_bpp(fb);
	printf("pixels = %X \n", render->pixels);
	for(int i=0;i<height;i++){
		for(int j=0;j<width;j++){
			((u32_t *)render->pixels)[i*width+j] = 0x00ff0000;
		}
	}
	framebuffer_present_render(fb, render, NULL, 0);
	framebuffer_set_backlight(fb, CONFIG_MAX_BRIGHTNESS);

	task_init();
	printf("do_exitcalls\n");
	while(1);
	/* Do all exit calls */
	do_exitcalls();
	while (1);
}
