/* SPDX-License-Identifier: GPL-2.0 */
/* Derived from Hardkernel linux 5aed95d35d252cafc75ce613a3a0052285662de2,
 * drivers/amlogic/amports/encoder.c. Original author: Simon Zheng.
 * This file is register setup only; it performs no allocation or user I/O.
 */
static void avc_prot_init(encode_wq_t*wq, int quant,bool IDR)
{
    unsigned int data32;
    int pic_width, pic_height;
    int pic_mb_nr;
    int pic_mbx, pic_mby;
    int i_pic_qp, p_pic_qp;
    int i_pic_qp_c, p_pic_qp_c;
    pic_width  = wq->pic.encoder_width;
    pic_height = wq->pic.encoder_height;
    pic_mb_nr  = 0;
    pic_mbx    = 0;
    pic_mby    = 0;
    i_pic_qp   = quant;
    p_pic_qp   = quant;
    WRITE_HREG(VLC_PIC_SIZE, pic_width | (pic_height<<16));
    WRITE_HREG(VLC_PIC_POSITION, (pic_mb_nr<<16) | (pic_mby << 8) |  (pic_mbx <<0));	//start mb

    switch (i_pic_qp) {    // synopsys parallel_case full_case
        case 0 : i_pic_qp_c = 0; break;
        case 1 : i_pic_qp_c = 1; break;
        case 2 : i_pic_qp_c = 2; break;
        case 3 : i_pic_qp_c = 3; break;
        case 4 : i_pic_qp_c = 4; break;
        case 5 : i_pic_qp_c = 5; break;
        case 6 : i_pic_qp_c = 6; break;
        case 7 : i_pic_qp_c = 7; break;
        case 8 : i_pic_qp_c = 8; break;
        case 9 : i_pic_qp_c = 9; break;
        case 10 : i_pic_qp_c = 10; break;
        case 11 : i_pic_qp_c = 11; break;
        case 12 : i_pic_qp_c = 12; break;
        case 13 : i_pic_qp_c = 13; break;
        case 14 : i_pic_qp_c = 14; break;
        case 15 : i_pic_qp_c = 15; break;
        case 16 : i_pic_qp_c = 16; break;
        case 17 : i_pic_qp_c = 17; break;
        case 18 : i_pic_qp_c = 18; break;
        case 19 : i_pic_qp_c = 19; break;
        case 20 : i_pic_qp_c = 20; break;
        case 21 : i_pic_qp_c = 21; break;
        case 22 : i_pic_qp_c = 22; break;
        case 23 : i_pic_qp_c = 23; break;
        case 24 : i_pic_qp_c = 24; break;
        case 25 : i_pic_qp_c = 25; break;
        case 26 : i_pic_qp_c = 26; break;
        case 27 : i_pic_qp_c = 27; break;
        case 28 : i_pic_qp_c = 28; break;
        case 29 : i_pic_qp_c = 29; break;
        case 30 : i_pic_qp_c = 29; break;
        case 31 : i_pic_qp_c = 30; break;
        case 32 : i_pic_qp_c = 31; break;
        case 33 : i_pic_qp_c = 32; break;
        case 34 : i_pic_qp_c = 32; break;
        case 35 : i_pic_qp_c = 33; break;
        case 36 : i_pic_qp_c = 34; break;
        case 37 : i_pic_qp_c = 34; break;
        case 38 : i_pic_qp_c = 35; break;
        case 39 : i_pic_qp_c = 35; break;
        case 40 : i_pic_qp_c = 36; break;
        case 41 : i_pic_qp_c = 36; break;
        case 42 : i_pic_qp_c = 37; break;
        case 43 : i_pic_qp_c = 37; break;
        case 44 : i_pic_qp_c = 37; break;
        case 45 : i_pic_qp_c = 38; break;
        case 46 : i_pic_qp_c = 38; break;
        case 47 : i_pic_qp_c = 38; break;
        case 48 : i_pic_qp_c = 39; break;
        case 49 : i_pic_qp_c = 39; break;
        case 50 : i_pic_qp_c = 39; break;
        default : i_pic_qp_c = 39; break; // should only be 51 or more (when index_offset)
    }

    switch (p_pic_qp) {    // synopsys parallel_case full_case
        case 0 : p_pic_qp_c = 0; break;
        case 1 : p_pic_qp_c = 1; break;
        case 2 : p_pic_qp_c = 2; break;
        case 3 : p_pic_qp_c = 3; break;
        case 4 : p_pic_qp_c = 4; break;
        case 5 : p_pic_qp_c = 5; break;
        case 6 : p_pic_qp_c = 6; break;
        case 7 : p_pic_qp_c = 7; break;
        case 8 : p_pic_qp_c = 8; break;
        case 9 : p_pic_qp_c = 9; break;
        case 10 : p_pic_qp_c = 10; break;
        case 11 : p_pic_qp_c = 11; break;
        case 12 : p_pic_qp_c = 12; break;
        case 13 : p_pic_qp_c = 13; break;
        case 14 : p_pic_qp_c = 14; break;
        case 15 : p_pic_qp_c = 15; break;
        case 16 : p_pic_qp_c = 16; break;
        case 17 : p_pic_qp_c = 17; break;
        case 18 : p_pic_qp_c = 18; break;
        case 19 : p_pic_qp_c = 19; break;
        case 20 : p_pic_qp_c = 20; break;
        case 21 : p_pic_qp_c = 21; break;
        case 22 : p_pic_qp_c = 22; break;
        case 23 : p_pic_qp_c = 23; break;
        case 24 : p_pic_qp_c = 24; break;
        case 25 : p_pic_qp_c = 25; break;
        case 26 : p_pic_qp_c = 26; break;
        case 27 : p_pic_qp_c = 27; break;
        case 28 : p_pic_qp_c = 28; break;
        case 29 : p_pic_qp_c = 29; break;
        case 30 : p_pic_qp_c = 29; break;
        case 31 : p_pic_qp_c = 30; break;
        case 32 : p_pic_qp_c = 31; break;
        case 33 : p_pic_qp_c = 32; break;
        case 34 : p_pic_qp_c = 32; break;
        case 35 : p_pic_qp_c = 33; break;
        case 36 : p_pic_qp_c = 34; break;
        case 37 : p_pic_qp_c = 34; break;
        case 38 : p_pic_qp_c = 35; break;
        case 39 : p_pic_qp_c = 35; break;
        case 40 : p_pic_qp_c = 36; break;
        case 41 : p_pic_qp_c = 36; break;
        case 42 : p_pic_qp_c = 37; break;
        case 43 : p_pic_qp_c = 37; break;
        case 44 : p_pic_qp_c = 37; break;
        case 45 : p_pic_qp_c = 38; break;
        case 46 : p_pic_qp_c = 38; break;
        case 47 : p_pic_qp_c = 38; break;
        case 48 : p_pic_qp_c = 39; break;
        case 49 : p_pic_qp_c = 39; break;
        case 50 : p_pic_qp_c = 39; break;
        default : p_pic_qp_c = 39; break; // should only be 51 or more (when index_offset)
    }
    WRITE_HREG(QDCT_Q_QUANT_I,
                (i_pic_qp_c<<22) |
                (i_pic_qp<<16) |
                ((i_pic_qp_c%6)<<12)|((i_pic_qp_c/6)<<8)|((i_pic_qp%6)<<4)|((i_pic_qp/6)<<0));

    WRITE_HREG(QDCT_Q_QUANT_P,
                (p_pic_qp_c<<22) |
                (p_pic_qp<<16) |
                ((p_pic_qp_c%6)<<12)|((p_pic_qp_c/6)<<8)|((p_pic_qp%6)<<4)|((p_pic_qp/6)<<0));

    //avc_init_input_buffer();

    WRITE_HREG(IGNORE_CONFIG ,
                (1<<31) | // ignore_lac_coeff_en
                (1<<26) | // ignore_lac_coeff_else (<1)
                (1<<21) | // ignore_lac_coeff_2 (<1)
                (2<<16) | // ignore_lac_coeff_1 (<2)
                (1<<15) | // ignore_cac_coeff_en
                (1<<10) | // ignore_cac_coeff_else (<1)
                (1<<5)  | // ignore_cac_coeff_2 (<1)
                (2<<0));    // ignore_cac_coeff_1 (<2)

    WRITE_HREG(IGNORE_CONFIG_2,
                (1<<31) | // ignore_t_lac_coeff_en
                (1<<26) | // ignore_t_lac_coeff_else (<1)
                (1<<21) | // ignore_t_lac_coeff_2 (<1)
                (5<<16) | // ignore_t_lac_coeff_1 (<5)
                (0<<0));

    WRITE_HREG(QDCT_MB_CONTROL,
                (1<<9) | // mb_info_soft_reset
                (1<<0)); // mb read buffer soft reset

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        WRITE_HREG(QDCT_MB_CONTROL,
                  (0<<28) | // ignore_t_p8x8
                  (0<<27) | // zero_mc_out_null_non_skipped_mb
                  (0<<26) | // no_mc_out_null_non_skipped_mb
                  (0<<25) | // mc_out_even_skipped_mb
                  (0<<24) | // mc_out_wait_cbp_ready
                  (0<<23) | // mc_out_wait_mb_type_ready
                  (1<<29) | // ie_start_int_enable
                  (1<<19) | // i_pred_enable
                  (1<<20) | // ie_sub_enable
                  (1<<18) | // iq_enable
                  (1<<17) | // idct_enable
                  (1<<14) | // mb_pause_enable
                  (1<<13) | // q_enable
                  (1<<12) | // dct_enable
                  (1<<10) | // mb_info_en
                  (0<<3) | // endian
                  (0<<1) | // mb_read_en
                  (0<<0));   // soft reset
    }else{
        WRITE_HREG(QDCT_MB_CONTROL,
                  (0<<28) | // ignore_t_p8x8
                  (0<<27) | // zero_mc_out_null_non_skipped_mb
                  (0<<26) | // no_mc_out_null_non_skipped_mb
                  (0<<25) | // mc_out_even_skipped_mb
                  (0<<24) | // mc_out_wait_cbp_ready
                  (0<<23) | // mc_out_wait_mb_type_ready
                  (1<<22) | // i_pred_int_enable
                  (1<<19) | // i_pred_enable
                  (1<<20) | // ie_sub_enable
                  (1<<18) | // iq_enable
                  (1<<17) | // idct_enable
                  (1<<14) | // mb_pause_enable
                  (1<<13) | // q_enable
                  (1<<12) | // dct_enable
                  (1<<10) | // mb_info_en
                  (avc_endian<<3) | // endian
                  (1<<1) | // mb_read_en
                  (0<<0));   // soft reset
    }

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        int me_mode  = (ie_me_mode >>ME_PIXEL_MODE_SHIFT)&ME_PIXEL_MODE_MASK;
        WRITE_HREG(SAD_CONTROL,
                  (0<<3) | // ie_result_buff_enable
                  (1<<2) | // ie_result_buff_soft_reset
                  (0<<1) | // sad_enable
                  (1<<0));   // sad soft reset

        WRITE_HREG(IE_RESULT_BUFFER, 0);

        WRITE_HREG(SAD_CONTROL,
                  (1<<3) | // ie_result_buff_enable
                  (0<<2) | // ie_result_buff_soft_reset
                  (1<<1) | // sad_enable
                  (0<<0));   // sad soft reset

        WRITE_HREG(IE_CONTROL,
                  (0<<1) | // ie_enable
                  (1<<0));   // ie soft reset

        WRITE_HREG(IE_CONTROL,
                  (0<<1) | // ie_enable
                  (0<<0)); // ie soft reset

        WRITE_HREG(ME_SAD_ENOUGH_01,me_sad_enough_01);
                  //(0x18<<12) | // me_sad_enough_1
                  //(0x10<<0)); // me_sad_enough_0

        WRITE_HREG(ME_SAD_ENOUGH_23, me_sad_enough_23);
                  //(0x20<<0) | // me_sad_enough_2
                  //(0<<12)); // me_sad_enough_3

        WRITE_HREG(ME_STEP0_CLOSE_MV,
                  (0x100 << 10) | // me_step0_big_sad -- two MV sad diff bigger will use use 1
                  (2<<5) | // me_step0_close_mv_y
                  (2<<0));   // me_step0_close_mv_x

        if(me_mode == 3){
            WRITE_HREG(ME_SKIP_LINE,
                      ( 8 << 24) |  // step_3_skip_line
                      ( 8 << 18) |  // step_2_skip_line
                      ( 2 << 12) |  // step_1_skip_line
                      ( 0 << 6) |  // step_0_skip_line
                      //(8 <<0); // read 8*2 less line to save bandwidth
                      (0 <<0)); // read 8*2 less line to save bandwidth

            WRITE_HREG(ME_F_SKIP_SAD,
                      ( 0x00 << 24) |  // force_skip_sad_3
                      ( 0x00 << 16) |  // force_skip_sad_2
                      ( 0x30 << 8)  |  // force_skip_sad_1
                      ( 0x10 << 0));    // force_skip_sad_0

            WRITE_HREG(ME_F_SKIP_WEIGHT,
                      ( 0x00 << 24) |  // force_skip_weight_3
                      ( 0x08 << 16) |  // force_skip_weight_2
                      ( 0x18 << 8)  |  // force_skip_weight_1
                      ( 0x18 << 0));    // force_skip_weight_0
        }else{
            WRITE_HREG(ME_SKIP_LINE,
                      ( 4 << 24) |  // step_3_skip_line
                      ( 4 << 18) |  // step_2_skip_line
                      ( 2 << 12) |  // step_1_skip_line
                      ( 0 << 6) |  // step_0_skip_line
                      //(8 <<0); // read 8*2 less line to save bandwidth
                      (0 <<0)); // read 8*2 less line to save bandwidth

            WRITE_HREG(ME_F_SKIP_SAD,
                      ( 0x40 << 24) |  // force_skip_sad_3
                      //( 0x40 << 16) |  // force_skip_sad_2
                      ( 0x30 << 16) |  // force_skip_sad_2
                      ( 0x30 << 8)  |  // force_skip_sad_1
                      ( 0x10 << 0));    // force_skip_sad_0

            WRITE_HREG(ME_F_SKIP_WEIGHT,
                      ( 0x18 << 24) |  // force_skip_weight_3
                      ( 0x18 << 16) |  // force_skip_weight_2
                      ( 0x18 << 8)  |  // force_skip_weight_1
                      ( 0x18 << 0));    // force_skip_weight_0
        }

        WRITE_HREG(IE_DATA_FEED_BUFF_INFO,0);
    }

    WRITE_HREG(HCODEC_CURR_CANVAS_CTRL,0);
    data32 = READ_HREG(VLC_CONFIG);
    data32 = data32 | (1<<0); // set pop_coeff_even_all_zero
    WRITE_HREG(VLC_CONFIG , data32);

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        if(IDR){
            WRITE_HREG(BITS_INFO_DDR_START, wq->mem.intra_bits_info_ddr_start_addr);
            WRITE_HREG(MV_INFO_DDR_START, wq->mem.intra_pred_info_ddr_start_addr);
        }else{
            WRITE_HREG(BITS_INFO_DDR_START, wq->mem.inter_bits_info_ddr_start_addr);
            WRITE_HREG(MV_INFO_DDR_START, wq->mem.inter_mv_info_ddr_start_addr);
        }
    }else{
        WRITE_HREG(SW_CTL_INFO_DDR_START, wq->mem.sw_ctl_info_start_addr);
    }
    /* clear mailbox interrupt */
    WRITE_HREG(HCODEC_IRQ_MBOX_CLR, 1);

    /* enable mailbox interrupt */
    WRITE_HREG(HCODEC_IRQ_MBOX_MASK, 1);
}
