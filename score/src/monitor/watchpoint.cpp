
#include <stdint.h>
#include <cstring>
#include <cstdio>

#include "../../include/expr.h"
#include "../../include/watchpoint.h"

struct Watchpoint {
    int      id;
    char     expr[128];
    uint32_t last_val;
    bool     in_use;
};

static Watchpoint wp_pool[32];
static int        next_id = 1;

int wp_add(const char *expr_str) 
{
    for(int i=0; i<32; i++)
    {
        if(wp_pool[i].in_use == false)
        {
            wp_pool[i].id = next_id;
            next_id++;
            strncpy(wp_pool[i].expr, expr_str, sizeof(wp_pool[i].expr));
            wp_pool[i].expr[sizeof(wp_pool[i].expr)-1] = '\0';
            bool success;
            wp_pool[i].last_val = expr_eval(wp_pool[i].expr, &success);
            if(!success)
            {
                printf("wp_add: expr_eval failed\n");
                return -1;
            }
            wp_pool[i].in_use = true;
            return wp_pool[i].id;
        }
    } 
    return -1;
}

bool wp_delete(int id) 
{
    for(int i=0; i<32; i++)
    {
        if(wp_pool[i].in_use == true && wp_pool[i].id == id)
        {
            wp_pool[i].in_use = false;
            return true;
        }
    }
    return false;
}

void wp_print_all() 
{
    bool has_watchpoints = false;
    for (int i = 0; i < 32; i++) {
        if (wp_pool[i].in_use)
        {
            has_watchpoints = true;
            break;
        }
    }
    if (!has_watchpoints) {
        printf("No watchpoints.\n");
        return;
    }

    printf("  Num  Expression          Last Value\n");
    printf("  ---  ------------------  ----------\n");
    for (int i = 0; i < 32; i++) 
    {
        if (wp_pool[i].in_use) {
            printf("  %-3d  %-18s  0x%08x\n", 
                   wp_pool[i].id, 
                   wp_pool[i].expr, 
                   wp_pool[i].last_val);
        }
    }
}

bool wp_check()
{
    bool trigger = false;
    for(int i=0; i<32; i++)
    {
        if(wp_pool[i].in_use == true)
        {
            bool success;
            uint32_t val = expr_eval(wp_pool[i].expr, &success);
            if(!success)
            {
                printf("Error: {watch_point[%d]: %s} expr_eval failed\n", wp_pool[i].id,wp_pool[i].expr);
               continue;
            }
            if(val != wp_pool[i].last_val)
            {
                printf("watchpoint[%d] triggered: %s\n", wp_pool[i].id, wp_pool[i].expr);
                printf("old value = 0x%08x, new value = 0x%08x\n", wp_pool[i].last_val, val);
                wp_pool[i].last_val = val;
                trigger = true;
            }
        }
    }
    return trigger;
}
