/* Routine to check correctness of the results */
static int
chk_128 (int *v1, int *v2)
{
  int i;
  int n_fails = 0;

  for (i = 0; i < 4; i++)
    if (v1[i] != v2[i])
      n_fails += 1;

  return n_fails;
}

static int vals [256] __attribute__ ((aligned(16))) =
{
  0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x5be800ee, 0x4f2d7b15,
  0x409d9291, 0xdd95f27f, 0x423986e3, 0x21a4d2cd, 0xa7056d84, 0x4f4e5a3b,
  0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
  0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
  0x73ef0244, 0xcd836329, 0x847f634f, 0xa7e3abcf, 0xb4c14764, 0x1ef42c06,
  0x504f29ac, 0x4ae7ca73, 0xaddde3c9, 0xf63ded2e, 0xa5d3553d, 0xa52ae05f,
  0x6fd3c83a, 0x7dc2b300, 0x76b05de7, 0xea8ebae5, 0x549568dd, 0x172f0358,
  0x917eadf0, 0x796fb0a7, 0xb39381af, 0xd0591d61, 0x731d2f17, 0xbc4b6f5d,
  0x8ec664c2, 0x3c199c19, 0x9c81db12, 0x6d85913b, 0x486107a9, 0xab6f4b26,
  0x5630d37c, 0x20836e85, 0x40d4e746, 0xdfbaba36, 0xbeacaa69, 0xb3c84083,
  0x8a688eb4, 0x08cde481, 0x66e7a190, 0x74ee1639, 0xb3942a19, 0xe0c40471,
  0x9b789489, 0x9751207a, 0x543a1524, 0x41da7ad6, 0x614bb563, 0xf86f57b1,
  0x69e62199, 0x2150cb12, 0x9ed74062, 0x429471f4, 0xad28502b, 0xf2e2d4d5,
  0x45b6ce09, 0xaaa5e649, 0xb46da484, 0x0a637515, 0xae7a3212, 0x5afc784c,
  0x776cfbbe, 0x9c542bb2, 0x64193aa8, 0x16e8a655, 0x4e3d2f92, 0xe05d7b72,
  0x89854ebc, 0x8c318814, 0xb81e76e0, 0x3f2625f5, 0x61b44852, 0x5209d7ad,
  0x842fe317, 0xd3cfcca1, 0x8d287cc7, 0x80f0c9a8, 0x4215f4e5, 0x563993d6,
  0x5d627433, 0xc4449e35, 0x5b4fe009, 0x3ef92286, 0xacbc8927, 0x549ab870,
  0x9ac5b959, 0xed8f1c91, 0x7ecf02cd, 0x989c0e8b, 0xa31d6918, 0x1dc2bcc1,
  0x99d3f3cc, 0x6857acc8, 0x45d7324a, 0xaebdf2e6, 0x7af2f2ae, 0x09716f73,
  0x7816e694, 0xc65493c0, 0x9f7e87bc, 0xaa96cd40, 0xbfb5bfc6, 0x01a2cce7,
  0x5f1d8c46, 0x45303efb, 0xb24607c3, 0xef2009a7, 0xba873753, 0xbefb14bc,
  0x74e53cd3, 0x70124708, 0x6eb4bdbd, 0xf3ba5e43, 0x4c94085f, 0x0c03e7e0,
  0x9a084931, 0x62735424, 0xaeee77c5, 0xdb34f90f, 0x6860cbdd, 0xaf77cf9f,
  0x95b28158, 0x23bd70d7, 0x9fbc3d88, 0x742e659e, 0x53bcfb48, 0xb8a63f6c,
  0x4dcf3373, 0x2b168627, 0x4fe20745, 0xd0af5e94, 0x22514e6a, 0xb8ef25c2,
  0x89ec781a, 0x13d9002b, 0x6d724500, 0x7fdbf63f, 0xb0e9ced5, 0xf919e0f3,
  0x00fef203, 0x8905d47a, 0x434e7517, 0x4aef8e2c, 0x689f51e8, 0xe513b7c3,
  0x72bbc5d2, 0x3a222f74, 0x05c3a0f9, 0xd5489d82, 0xb41fbe83, 0xec5d305f,
  0x5ea02b0b, 0xb176065b, 0xa8eb404e, 0x80349117, 0x210fd49e, 0x43898d0e,
  0x6c151b9c, 0x8742df18, 0x7b64de73, 0x1dbf52b2, 0x55c9cb19, 0xeb841f10,
  0x10b8ae76, 0x0764ecb6, 0xb7479018, 0x2672cb3f, 0x7ac9ac90, 0x4be5332c,
  0x8f1a0615, 0x4efb7a77, 0x16551a85, 0xdb2c3d66, 0x49179c07, 0x5dc4657e,
  0x5e76907e, 0xd7486a9c, 0x445204a4, 0x65cdc426, 0x33f86ded, 0xcba95dda,
  0x83351f16, 0xfedefad9, 0x639b620f, 0x86896a64, 0xba4099ba, 0x965f4a21,
  0x1247154f, 0x25604c42, 0x5862d692, 0xb1e9149e, 0x612516a5, 0x02c49bf8,
  0x631212bf, 0x9f69f54e, 0x168b63b0, 0x310a25ba, 0xa42a59cd, 0x084f0af9,
  0x44a06cec, 0x5c0cda40, 0xb932d721, 0x7c42bb0d, 0x213cd3f0, 0xedc7f5a4,
  0x7fb85859, 0x6b3da5ea, 0x61cd591e, 0xe8e9aa08, 0x4361fc34, 0x53d40d2a,
  0x0511ad1b, 0xf996b44c, 0xb5ead756, 0xc022138d, 0x6172adf1, 0xa4a0a3b4,
  0x8c2977b8, 0xa8e482ed, 0x04fcdd6b, 0x3f7b85d4, 0x4fca1e46, 0xa392ddca,
  0x569fc791, 0x346a706c, 0x543bf3eb, 0x895b3cde, 0x2146bb80, 0x26b3c168,
  0x929998db, 0x1ea472c9, 0x7207b36b, 0x6a8f10d4
};
