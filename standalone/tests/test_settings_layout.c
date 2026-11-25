#include "settings_layout.h"
#include <stdio.h>
/* Geometry guard for the Settings screen. main.c draws the screen from
 * the SET_* macros in settings_layout.h; this test rebuilds every
 * element rect the draw code can produce (worst case: every optional
 * line present, focus glow drawn) from those same macros and asserts
 * the layout can never overlap itself again (the 1.15 defect: the
 * updates row's second description line ran into the reset tile).
 *
 * Text is modelled as a box of height size+6: ui_text puts the
 * baseline at y+size, so glyphs occupy roughly [y, y+size] with a few
 * px of descender below the baseline. A row label is the one element
 * allowed inside another rect: it must be fully contained in its own
 * tile, whose checked rect is the focus glow (face + 3 px on every
 * side, as draw_gc_tile draws a selected row). */
static int fails;
#define CHECK(cond) do { if(!(cond)) { printf("FAIL line %d: %s\n",__LINE__,#cond); fails++; } } while(0)
enum { K_GLOW, K_LABEL, K_OTHER };
typedef struct { const char *name; int x,y,w,h; int row,kind; } Elem;
static Elem elems[32];
static int count;
static void add(const char *name,int x,int y,int w,int h,int row,int kind) {
    elems[count].name=name; elems[count].x=x; elems[count].y=y;
    elems[count].w=w; elems[count].h=h; elems[count].row=row; elems[count].kind=kind; count++;
}
static int intersects(const Elem *a,const Elem *b) {
    return a->x < b->x+b->w && b->x < a->x+a->w && a->y < b->y+b->h && b->y < a->y+a->h;
}
static int contains(const Elem *outer,const Elem *inner) {
    return inner->x>=outer->x && inner->y>=outer->y &&
           inner->x+inner->w<=outer->x+outer->w && inner->y+inner->h<=outer->y+outer->h;
}
#define TEXT_W (SET_X+SET_W-SET_TEXT_X)
int main(void) {
    /* Pin the scheme: four rows on a uniform 110 px pitch. */
    CHECK(SET_ROW_Y(0)==138 && SET_ROW_Y(1)==248 && SET_ROW_Y(2)==358 && SET_ROW_Y(3)==468);
    for(int i=0;i<3;i++) CHECK(SET_ROW_Y(i+1)-SET_ROW_Y(i)==SET_PITCH);
    for(int i=0;i<4;i++) {
        char gname[40],lname[40];
        snprintf(gname,sizeof(gname),"tile%d-glow",i);
        add(gname,SET_X-3,SET_ROW_Y(i)-3,SET_W+6,SET_TILE_H+6,i,K_GLOW);
        snprintf(lname,sizeof(lname),"tile%d-label",i);
        add(lname,SET_TEXT_X,SET_ROW_Y(i)+SET_LABEL_DY,TEXT_W,26+6,i,K_LABEL);
    }
    add("volume-bar",SET_TEXT_X,SET_BAR_Y(0),SET_BAR_W,SET_BAR_H,0,K_OTHER);
    add("volume-hint",SET_TEXT_X,SET_HINT_Y(0),SET_BAR_W,18+6,0,K_OTHER);
    add("ftp-info",SET_TEXT_X,SET_DESC_Y(1),TEXT_W,18+6,1,K_OTHER);
    add("updates-line1",SET_TEXT_X,SET_DESC_Y(2),TEXT_W,18+6,2,K_OTHER);
    add("updates-line2",SET_TEXT_X,SET_DESC_Y(2)+SET_LINE,TEXT_W,18+6,2,K_OTHER);
    add("reset-desc",SET_TEXT_X,SET_DESC_Y(3),TEXT_W,18+6,3,K_OTHER);
    add("reset-perf",SET_TEXT_X,SET_DESC_Y(3)+SET_LINE,TEXT_W,18+6,3,K_OTHER);
    add("reset-notice",SET_TEXT_X,SET_DESC_Y(3)+2*SET_LINE,TEXT_W,18+6,3,K_OTHER);
    for(int i=0;i<count;i++) {
        const Elem *e=&elems[i];
        CHECK(e->x>=0 && e->x+e->w<=1280);
        CHECK(e->y>=0 && e->y+e->h<=SET_FOOTER_Y);
        if(e->kind==K_LABEL) {
            /* The label must sit inside its own tile's face. */
            Elem face={"tile-face",SET_X,SET_ROW_Y(e->row),SET_W,SET_TILE_H,e->row,K_OTHER};
            if(!contains(&face,e)) { printf("FAIL: %s not contained in tile %d\n",e->name,e->row); fails++; }
        }
        for(int j=i+1;j<count;j++) {
            const Elem *o=&elems[j];
            int label_in_own_glow=(e->row==o->row) &&
                ((e->kind==K_LABEL && o->kind==K_GLOW) || (e->kind==K_GLOW && o->kind==K_LABEL));
            if(label_in_own_glow) continue;
            if(intersects(e,o)) { printf("FAIL: %s [%d,%d %dx%d] overlaps %s [%d,%d %dx%d]\n",e->name,e->x,e->y,e->w,e->h,o->name,o->x,o->y,o->w,o->h); fails++; }
        }
    }
    if(fails) { printf("Settings layout tests: %d failures\n",fails); return 1; }
    printf("Settings layout: %d elements, uniform pitch, no overlaps, all above the footer\n",count);
    return 0;
}
