// SPDX-License-Identifier: GPL-2.0
/* Offline investigation harness. Selection functions are extracted verbatim
 * from matching Linux 5.15.79 sources; node wiring and arithmetic adapters
 * model only the Meson8b VDEC1 -> parent selection under examination.
 * All operations execute on the host; no MMIO, target calls or clock writes.
 * Flags are local test values and map to the corresponding kernel flags.
 */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
typedef uint64_t u64;typedef uint8_t u8;
#define SDM_DEN 16384
#define N2_MIN 4
#define N2_MAX 511
#define CLK_MESON_MPLL_ROUND_CLOSEST 0x1
#define DIV_ROUND_CLOSEST_ULL(a,b) (((a)+(b)/2)/(b))
#define do_div(n,base) ({uint64_t remainder=(n)%(base);(n)/=(base);remainder;})
#define CLK_SET_RATE_PARENT 1
#define CLK_SET_RATE_NO_REPARENT 2
#define CLK_MUX_ROUND_CLOSEST 4
#define CLK_DIVIDER_ROUND_CLOSEST 8
#define CLK_DIVIDER_POWER_OF_TWO 16
#define ULONG_MAX 0xffffffffUL
#define min(a,b) ((a)<(b)?(a):(b))
#define DIV_ROUND_UP_ULL(a,b) (((a)+(b)-1)/(b))
#define abs(a) llabs((int32_t)(uint32_t)(a))
struct clk_hw;struct clk_core {struct clk_hw *hw;struct clk_core *parent;struct clk_core **parents;unsigned long flags;int num_parents;};
struct clk_hw {struct clk_core *core;unsigned long rate;int kind;};
struct clk_rate_request {unsigned long rate,min_rate,max_rate,best_parent_rate;struct clk_hw *best_parent_hw;};
struct clk_div_table {int unused;};
static unsigned long clk_core_get_rate_nolock(struct clk_core *c){return c->hw->rate;}
static struct clk_core *clk_core_get_parent_by_index(struct clk_core *c,int i){return c->parents[i];}
static unsigned long clk_hw_get_flags(struct clk_hw *hw){return hw->core->flags;}
static int __clk_determine_rate(struct clk_hw *,struct clk_rate_request *);
static unsigned long clk_hw_round_rate(struct clk_hw *,unsigned long);
static unsigned long _get_maxdiv(const struct clk_div_table *t,unsigned int width,unsigned long flags){(void)t;(void)flags;return 1UL<<width;}
static unsigned int _div_round(const struct clk_div_table *t,unsigned long parent,unsigned long rate,unsigned long flags){(void)t;(void)flags;return (parent+rate/2)/rate;}
static int _round_up_table(const struct clk_div_table *t,int div){(void)t;return div;}
static unsigned int __roundup_pow_of_two(unsigned int v){unsigned int p=1;while(p<v)p*=2;return p;}
static bool mux_is_better_rate(unsigned long rate, unsigned long now,
			   unsigned long best, unsigned long flags)
{
	if (flags & CLK_MUX_ROUND_CLOSEST)
		return abs(now - rate) < abs(best - rate);

	return now <= rate && now > best;
}
int clk_mux_determine_rate_flags(struct clk_hw *hw,
				 struct clk_rate_request *req,
				 unsigned long flags)
{
	struct clk_core *core = hw->core, *parent, *best_parent = NULL;
	int i, num_parents, ret;
	unsigned long best = 0;
	struct clk_rate_request parent_req = *req;

	/* if NO_REPARENT flag set, pass through to current parent */
	if (core->flags & CLK_SET_RATE_NO_REPARENT) {
		parent = core->parent;
		if (core->flags & CLK_SET_RATE_PARENT) {
			ret = __clk_determine_rate(parent ? parent->hw : NULL,
						   &parent_req);
			if (ret)
				return ret;

			best = parent_req.rate;
		} else if (parent) {
			best = clk_core_get_rate_nolock(parent);
		} else {
			best = clk_core_get_rate_nolock(core);
		}

		goto out;
	}

	/* find the parent that can provide the fastest rate <= rate */
	num_parents = core->num_parents;
	for (i = 0; i < num_parents; i++) {
		parent = clk_core_get_parent_by_index(core, i);
		if (!parent)
			continue;

		if (core->flags & CLK_SET_RATE_PARENT) {
			parent_req = *req;
			ret = __clk_determine_rate(parent->hw, &parent_req);
			if (ret)
				continue;
		} else {
			parent_req.rate = clk_core_get_rate_nolock(parent);
		}

		if (mux_is_better_rate(req->rate, parent_req.rate,
				       best, flags)) {
			best_parent = parent;
			best = parent_req.rate;
		}
	}

	if (!best_parent)
		return -EINVAL;

out:
	if (best_parent)
		req->best_parent_hw = best_parent->hw;
	req->best_parent_rate = best;
	req->rate = best;

	return 0;
}
static bool _is_best_div(unsigned long rate, unsigned long now,
			 unsigned long best, unsigned long flags)
{
	if (flags & CLK_DIVIDER_ROUND_CLOSEST)
		return abs(rate - now) < abs(rate - best);

	return now <= rate && now > best;
}
static int _next_div(const struct clk_div_table *table, int div,
		     unsigned long flags)
{
	div++;

	if (flags & CLK_DIVIDER_POWER_OF_TWO)
		return __roundup_pow_of_two(div);
	if (table)
		return _round_up_table(table, div);

	return div;
}
static int clk_divider_bestdiv(struct clk_hw *hw, struct clk_hw *parent,
			       unsigned long rate,
			       unsigned long *best_parent_rate,
			       const struct clk_div_table *table, u8 width,
			       unsigned long flags)
{
	int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;
	unsigned long parent_rate_saved = *best_parent_rate;

	if (!rate)
		rate = 1;

	maxdiv = _get_maxdiv(table, width, flags);

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		parent_rate = *best_parent_rate;
		bestdiv = _div_round(table, parent_rate, rate, flags);
		bestdiv = bestdiv == 0 ? 1 : bestdiv;
		bestdiv = bestdiv > maxdiv ? maxdiv : bestdiv;
		return bestdiv;
	}

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);

	for (i = _next_div(table, 0, flags); i <= maxdiv;
					     i = _next_div(table, i, flags)) {
		if (rate * i == parent_rate_saved) {
			/*
			 * It's the most ideal case if the requested rate can be
			 * divided from parent clock without needing to change
			 * parent rate, so return the divider immediately.
			 */
			*best_parent_rate = parent_rate_saved;
			return i;
		}
		parent_rate = clk_hw_round_rate(parent, rate * i);
		now = DIV_ROUND_UP_ULL((u64)parent_rate, i);
		if (_is_best_div(rate, now, best, flags)) {
			bestdiv = i;
			best = now;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestdiv) {
		bestdiv = _get_maxdiv(table, width, flags);
		*best_parent_rate = clk_hw_round_rate(parent, 1);
	}

	return bestdiv;
}
static long rate_from_params(unsigned long parent_rate,
			     unsigned int sdm,
			     unsigned int n2)
{
	unsigned long divisor = (SDM_DEN * n2) + sdm;

	if (n2 < N2_MIN)
		return -EINVAL;

	return DIV_ROUND_UP_ULL((u64)parent_rate * SDM_DEN, divisor);
}
static void params_from_rate(unsigned long requested_rate,
			     unsigned long parent_rate,
			     unsigned int *sdm,
			     unsigned int *n2,
			     u8 flags)
{
	uint64_t div = parent_rate;
	uint64_t frac = do_div(div, requested_rate);

	frac *= SDM_DEN;

	if (flags & CLK_MESON_MPLL_ROUND_CLOSEST)
		*sdm = DIV_ROUND_CLOSEST_ULL(frac, requested_rate);
	else
		*sdm = DIV_ROUND_UP_ULL(frac, requested_rate);

	if (*sdm == SDM_DEN) {
		*sdm = 0;
		div += 1;
	}

	if (div < N2_MIN) {
		*n2 = N2_MIN;
		*sdm = 0;
	} else if (div > N2_MAX) {
		*n2 = N2_MAX;
		*sdm = SDM_DEN - 1;
	} else {
		*n2 = div;
	}
}

static unsigned long mpll_round(unsigned long requested){
 unsigned int sdm,n2;params_from_rate(requested,2550000000,&sdm,&n2,0);
 return rate_from_params(2550000000,sdm,n2);
}
static int __clk_determine_rate(struct clk_hw *hw,struct clk_rate_request *r){
 if(hw->kind==0){r->rate=hw->rate;return 0;}
 if(hw->kind==1){r->rate=mpll_round(r->rate);return 0;}
 return clk_mux_determine_rate_flags(hw,r,CLK_MUX_ROUND_CLOSEST);
}
static unsigned long clk_hw_round_rate(struct clk_hw *hw,unsigned long rate){struct clk_rate_request r={.rate=rate};if(__clk_determine_rate(hw,&r))return 0;return r.rate;}
static const char *name(int i){const char *names[]={"fclk_div4","fclk_div3","fclk_div5","fclk_div7","mpll2","mpll1"};return names[i];}
int main(void){setvbuf(stdout,NULL,_IONBF,0);
 struct clk_hw parent[6];struct clk_core pc[6];struct clk_core *parents[6];unsigned long rates[]={637500000,850000000,510000000,364285714,499996410,0};
 for(int i=0;i<6;i++){pc[i]=(struct clk_core){.hw=&parent[i]};parent[i]=(struct clk_hw){.core=&pc[i],.rate=rates[i],.kind=i>=4?1:0};parents[i]=&pc[i];}
 struct clk_core muxcore={.parent=&pc[3],.parents=parents,.num_parents=6,.flags=CLK_SET_RATE_PARENT};struct clk_hw mux={.core=&muxcore,.rate=364285714,.kind=2};muxcore.hw=&mux;
 struct clk_core divcore={.parent=&muxcore,.flags=CLK_SET_RATE_PARENT};struct clk_hw divider={.core=&divcore};divcore.hw=&divider;
 unsigned long goals[]={182142857,255000000};
 for(int i=0;i<2;i++){
  unsigned long best_parent=364285714;int div=clk_divider_bestdiv(&divider,&mux,goals[i],&best_parent,NULL,7,CLK_DIVIDER_ROUND_CLOSEST);
  struct clk_rate_request req={.rate=best_parent};assert(!clk_mux_determine_rate_flags(&mux,&req,CLK_MUX_ROUND_CLOSEST));int index=-1;
  for(int j=0;j<6;j++)if(req.best_parent_hw==&parent[j])index=j;
  assert(index>=0);unsigned long result=DIV_ROUND_UP_ULL((u64)best_parent,div);
  printf("request=%lu divider=%d selected_parent=%s parent_rate=%lu output=%lu ethernet_tx_if_mpll2_used=%lu parent_index=%d encoded_index_with_mask_0x3=%d\n",goals[i],div,name(index),best_parent,result,index==4?best_parent/4:rates[4]/4,index,index&3);
  if(i==0)assert(div==2 && index==3 && best_parent==364285714);
  if(i==1)assert(div==1 && index==4 && best_parent==255000000 && result==255000000 && best_parent/4==63750000);
 }
 // Retargeting VDEC back onto a fixed parent does not reset an unselected MPLL.
 puts("Restoring only the decoder module does not restore a previously retuned Ethernet MPLL2.");
 return 0;
}
