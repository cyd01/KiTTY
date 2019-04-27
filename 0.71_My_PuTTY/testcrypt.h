/*
 * mpint.h functions.
 */
FUNC1(val_mpint, mp_new, uint)
FUNC1(void, mp_clear, val_mpint)
FUNC1(val_mpint, mp_from_bytes_le, val_string_ptrlen)
FUNC1(val_mpint, mp_from_bytes_be, val_string_ptrlen)
FUNC1(val_mpint, mp_from_integer, uint)
FUNC1(val_mpint, mp_from_decimal_pl, val_string_ptrlen)
FUNC1(val_mpint, mp_from_decimal, val_string_asciz)
FUNC1(val_mpint, mp_from_hex_pl, val_string_ptrlen)
FUNC1(val_mpint, mp_from_hex, val_string_asciz)
FUNC1(val_mpint, mp_copy, val_mpint)
FUNC1(val_mpint, mp_power_2, uint)
FUNC2(uint, mp_get_byte, val_mpint, uint)
FUNC2(uint, mp_get_bit, val_mpint, uint)
FUNC3(void, mp_set_bit, val_mpint, uint, uint)
FUNC1(uint, mp_max_bytes, val_mpint)
FUNC1(uint, mp_max_bits, val_mpint)
FUNC1(uint, mp_get_nbits, val_mpint)
FUNC1(val_string_asciz, mp_get_decimal, val_mpint)
FUNC1(val_string_asciz, mp_get_hex, val_mpint)
FUNC1(val_string_asciz, mp_get_hex_uppercase, val_mpint)
FUNC2(uint, mp_cmp_hs, val_mpint, val_mpint)
FUNC2(uint, mp_cmp_eq, val_mpint, val_mpint)
FUNC2(uint, mp_hs_integer, val_mpint, uint)
FUNC2(uint, mp_eq_integer, val_mpint, uint)
FUNC3(void, mp_min_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_max_into, val_mpint, val_mpint, val_mpint)
FUNC2(val_mpint, mp_min, val_mpint, val_mpint)
FUNC2(val_mpint, mp_max, val_mpint, val_mpint)
FUNC2(void, mp_copy_into, val_mpint, val_mpint)
FUNC4(void, mp_select_into, val_mpint, val_mpint, val_mpint, uint)
FUNC3(void, mp_add_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_sub_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_mul_into, val_mpint, val_mpint, val_mpint)
FUNC2(val_mpint, mp_add, val_mpint, val_mpint)
FUNC2(val_mpint, mp_sub, val_mpint, val_mpint)
FUNC2(val_mpint, mp_mul, val_mpint, val_mpint)
FUNC3(void, mp_and_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_or_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_xor_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_bic_into, val_mpint, val_mpint, val_mpint)
FUNC3(void, mp_add_integer_into, val_mpint, val_mpint, uint)
FUNC3(void, mp_sub_integer_into, val_mpint, val_mpint, uint)
FUNC3(void, mp_mul_integer_into, val_mpint, val_mpint, uint)
FUNC4(void, mp_cond_add_into, val_mpint, val_mpint, val_mpint, uint)
FUNC4(void, mp_cond_sub_into, val_mpint, val_mpint, val_mpint, uint)
FUNC3(void, mp_cond_swap, val_mpint, val_mpint, uint)
FUNC2(void, mp_cond_clear, val_mpint, uint)
FUNC4(void, mp_divmod_into, val_mpint, val_mpint, val_mpint, val_mpint)
FUNC2(val_mpint, mp_div, val_mpint, val_mpint)
FUNC2(val_mpint, mp_mod, val_mpint, val_mpint)
FUNC2(void, mp_reduce_mod_2to, val_mpint, uint)
FUNC2(val_mpint, mp_invert_mod_2to, val_mpint, uint)
FUNC2(val_mpint, mp_invert, val_mpint, val_mpint)
FUNC2(val_modsqrt, modsqrt_new, val_mpint, val_mpint)
/* The modsqrt functions' 'success' pointer becomes a second return value */
FUNC3(val_mpint, mp_modsqrt, val_modsqrt, val_mpint, out_uint)
FUNC1(val_monty, monty_new, val_mpint)
FUNC1(val_mpint, monty_modulus, val_monty)
FUNC1(val_mpint, monty_identity, val_monty)
FUNC3(void, monty_import_into, val_monty, val_mpint, val_mpint)
FUNC2(val_mpint, monty_import, val_monty, val_mpint)
FUNC3(void, monty_export_into, val_monty, val_mpint, val_mpint)
FUNC2(val_mpint, monty_export, val_monty, val_mpint)
FUNC4(void, monty_mul_into, val_monty, val_mpint, val_mpint, val_mpint)
FUNC3(val_mpint, monty_add, val_monty, val_mpint, val_mpint)
FUNC3(val_mpint, monty_sub, val_monty, val_mpint, val_mpint)
FUNC3(val_mpint, monty_mul, val_monty, val_mpint, val_mpint)
FUNC3(val_mpint, monty_pow, val_monty, val_mpint, val_mpint)
FUNC2(val_mpint, monty_invert, val_monty, val_mpint)
FUNC3(val_mpint, monty_modsqrt, val_modsqrt, val_mpint, out_uint)
FUNC3(val_mpint, mp_modpow, val_mpint, val_mpint, val_mpint)
FUNC3(val_mpint, mp_modmul, val_mpint, val_mpint, val_mpint)
FUNC3(val_mpint, mp_modadd, val_mpint, val_mpint, val_mpint)
FUNC3(val_mpint, mp_modsub, val_mpint, val_mpint, val_mpint)
FUNC2(val_mpint, mp_rshift_safe, val_mpint, uint)
FUNC3(void, mp_lshift_fixed_into, val_mpint, val_mpint, uint)
FUNC3(void, mp_rshift_fixed_into, val_mpint, val_mpint, uint)
FUNC2(val_mpint, mp_rshift_fixed, val_mpint, uint)
FUNC1(val_mpint, mp_random_bits, uint)
FUNC2(val_mpint, mp_random_in_range, val_mpint, val_mpint)

/*
 * ecc.h functions.
 */
FUNC4(val_wcurve, ecc_weierstrass_curve, val_mpint, val_mpint, val_mpint, opt_val_mpint)
FUNC1(val_wpoint, ecc_weierstrass_point_new_identity, val_wcurve)
FUNC3(val_wpoint, ecc_weierstrass_point_new, val_wcurve, val_mpint, val_mpint)
FUNC3(val_wpoint, ecc_weierstrass_point_new_from_x, val_wcurve, val_mpint, uint)
FUNC1(val_wpoint, ecc_weierstrass_point_copy, val_wpoint)
FUNC1(uint, ecc_weierstrass_point_valid, val_wpoint)
FUNC2(val_wpoint, ecc_weierstrass_add_general, val_wpoint, val_wpoint)
FUNC2(val_wpoint, ecc_weierstrass_add, val_wpoint, val_wpoint)
FUNC1(val_wpoint, ecc_weierstrass_double, val_wpoint)
FUNC2(val_wpoint, ecc_weierstrass_multiply, val_wpoint, val_mpint)
FUNC1(uint, ecc_weierstrass_is_identity, val_wpoint)
/* The output pointers in get_affine all become extra output values */
FUNC3(void, ecc_weierstrass_get_affine, val_wpoint, out_val_mpint, out_val_mpint)
FUNC3(val_mcurve, ecc_montgomery_curve, val_mpint, val_mpint, val_mpint)
FUNC2(val_mpoint, ecc_montgomery_point_new, val_mcurve, val_mpint)
FUNC1(val_mpoint, ecc_montgomery_point_copy, val_mpoint)
FUNC3(val_mpoint, ecc_montgomery_diff_add, val_mpoint, val_mpoint, val_mpoint)
FUNC1(val_mpoint, ecc_montgomery_double, val_mpoint)
FUNC2(val_mpoint, ecc_montgomery_multiply, val_mpoint, val_mpint)
FUNC2(void, ecc_montgomery_get_affine, val_mpoint, out_val_mpint)
FUNC4(val_ecurve, ecc_edwards_curve, val_mpint, val_mpint, val_mpint, opt_val_mpint)
FUNC3(val_epoint, ecc_edwards_point_new, val_ecurve, val_mpint, val_mpint)
FUNC3(val_epoint, ecc_edwards_point_new_from_y, val_ecurve, val_mpint, uint)
FUNC1(val_epoint, ecc_edwards_point_copy, val_epoint)
FUNC2(val_epoint, ecc_edwards_add, val_epoint, val_epoint)
FUNC2(val_epoint, ecc_edwards_multiply, val_epoint, val_mpint)
FUNC2(uint, ecc_edwards_eq, val_epoint, val_epoint)
FUNC3(void, ecc_edwards_get_affine, val_epoint, out_val_mpint, out_val_mpint)

/*
 * The ssh_hash abstraction. Note the 'consumed', indicating that
 * ssh_hash_final puts its input ssh_hash beyond use.
 *
 * ssh_hash_update is an invention of testcrypt, handled in the real C
 * API by the hash object also functioning as a BinarySink.
 */
FUNC1(opt_val_hash, ssh_hash_new, hashalg)
FUNC1(val_hash, ssh_hash_copy, val_hash)
FUNC1(val_string, ssh_hash_final, consumed_val_hash)
FUNC2(void, ssh_hash_update, val_hash, val_string_ptrlen)

/*
 * The ssh2_mac abstraction. Note the optional ssh_cipher parameter
 * to ssh2_mac_new. Also, again, I've invented an ssh2_mac_update so
 * you can put data into the MAC.
 */
FUNC2(val_mac, ssh2_mac_new, macalg, opt_val_cipher)
FUNC2(void, ssh2_mac_setkey, val_mac, val_string_ptrlen)
FUNC1(void, ssh2_mac_start, val_mac)
FUNC2(void, ssh2_mac_update, val_mac, val_string_ptrlen)
FUNC1(val_string, ssh2_mac_genresult, val_mac)

/*
 * The ssh_key abstraction. All the uses of BinarySink and
 * BinarySource in parameters are replaced with ordinary strings for
 * the testing API: new_priv_openssh just takes a string input, and
 * all the functions that output key and signature blobs do it by
 * returning a string.
 */
FUNC2(val_key, ssh_key_new_pub, keyalg, val_string_ptrlen)
FUNC3(val_key, ssh_key_new_priv, keyalg, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_key, ssh_key_new_priv_openssh, keyalg, val_string_binarysource)
FUNC2(opt_val_string_asciz, ssh_key_invalid, val_key, uint)
FUNC4(void, ssh_key_sign, val_key, val_string_ptrlen, uint, out_val_string_binarysink)
FUNC3(boolean, ssh_key_verify, val_key, val_string_ptrlen, val_string_ptrlen)
FUNC2(void, ssh_key_public_blob, val_key, out_val_string_binarysink)
FUNC2(void, ssh_key_private_blob, val_key, out_val_string_binarysink)
FUNC2(void, ssh_key_openssh_blob, val_key, out_val_string_binarysink)
FUNC1(val_string_asciz, ssh_key_cache_str, val_key)
FUNC2(uint, ssh_key_public_bits, keyalg, val_string_ptrlen)

/*
 * The ssh_cipher abstraction. The in-place encrypt and decrypt
 * functions are wrapped to replace them with versions that take one
 * string and return a separate string.
 */
FUNC1(opt_val_cipher, ssh_cipher_new, cipheralg)
FUNC2(void, ssh_cipher_setiv, val_cipher, val_string_ptrlen)
FUNC2(void, ssh_cipher_setkey, val_cipher, val_string_ptrlen)
FUNC2(val_string, ssh_cipher_encrypt, val_cipher, val_string_ptrlen)
FUNC2(val_string, ssh_cipher_decrypt, val_cipher, val_string_ptrlen)
FUNC3(val_string, ssh_cipher_encrypt_length, val_cipher, val_string_ptrlen, uint)
FUNC3(val_string, ssh_cipher_decrypt_length, val_cipher, val_string_ptrlen, uint)

/*
 * Integer Diffie-Hellman.
 */
FUNC1(val_dh, dh_setup_group, dh_group)
FUNC2(val_dh, dh_setup_gex, val_mpint, val_mpint)
FUNC1(uint, dh_modulus_bit_size, val_dh)
FUNC2(val_mpint, dh_create_e, val_dh, uint)
FUNC2(boolean, dh_validate_f, val_dh, val_mpint)
FUNC2(val_mpint, dh_find_K, val_dh, val_mpint)

/*
 * Elliptic-curve Diffie-Hellman.
 */
FUNC1(val_ecdh, ssh_ecdhkex_newkey, ecdh_alg)
FUNC2(void, ssh_ecdhkex_getpublic, val_ecdh, out_val_string_binarysink)
FUNC2(val_mpint, ssh_ecdhkex_getkey, val_ecdh, val_string_ptrlen)

/*
 * RSA key exchange.
 */
FUNC1(val_rsakex, ssh_rsakex_newkey, val_string_ptrlen)
FUNC1(uint, ssh_rsakex_klen, val_rsakex)
FUNC3(val_string, ssh_rsakex_encrypt, val_rsakex, hashalg, val_string_ptrlen)
FUNC3(val_mpint, ssh_rsakex_decrypt, val_rsakex, hashalg, val_string_ptrlen)

/*
 * Bare RSA keys as used in SSH-1. The construction API functions
 * write into an existing RSAKey object, so I've invented an 'rsa_new'
 * function to make one in the first place.
 */
FUNC0(val_rsa, rsa_new)
FUNC3(void, get_rsa_ssh1_pub, val_string_binarysource, val_rsa, rsaorder)
FUNC2(void, get_rsa_ssh1_priv, val_string_binarysource, val_rsa)
FUNC2(val_string, rsa_ssh1_encrypt, val_string_ptrlen, val_rsa)
FUNC2(val_mpint, rsa_ssh1_decrypt, val_mpint, val_rsa)
FUNC2(val_string, rsa_ssh1_decrypt_pkcs1, val_mpint, val_rsa)
FUNC1(val_string_asciz, rsastr_fmt, val_rsa)
FUNC1(val_string_asciz, rsa_ssh1_fingerprint, val_rsa)
FUNC3(void, rsa_ssh1_public_blob, out_val_string_binarysink, val_rsa, rsaorder)
FUNC1(int, rsa_ssh1_public_blob_len, val_string_ptrlen)

/*
 * The PRNG type. Similarly to hashes and MACs, I've invented an extra
 * function prng_seed_update for putting seed data into the PRNG's
 * exposed BinarySink.
 */
FUNC1(val_prng, prng_new, hashalg)
FUNC1(void, prng_seed_begin, val_prng)
FUNC2(void, prng_seed_update, val_prng, val_string_ptrlen)
FUNC1(void, prng_seed_finish, val_prng)
FUNC2(val_string, prng_read, val_prng, uint)
FUNC3(void, prng_add_entropy, val_prng, uint, val_string_ptrlen)

/*
 * Miscellaneous.
 */
FUNC2(val_wpoint, ecdsa_public, val_mpint, keyalg)
FUNC2(val_epoint, eddsa_public, val_mpint, keyalg)
FUNC2(val_string, des_encrypt_xdmauth, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_string, des_decrypt_xdmauth, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_string, des3_encrypt_pubkey, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_string, des3_decrypt_pubkey, val_string_ptrlen, val_string_ptrlen)
FUNC3(val_string, des3_encrypt_pubkey_ossh, val_string_ptrlen, val_string_ptrlen, val_string_ptrlen)
FUNC3(val_string, des3_decrypt_pubkey_ossh, val_string_ptrlen, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_string, aes256_encrypt_pubkey, val_string_ptrlen, val_string_ptrlen)
FUNC2(val_string, aes256_decrypt_pubkey, val_string_ptrlen, val_string_ptrlen)
FUNC1(uint, crc32_rfc1662, val_string_ptrlen)
FUNC1(uint, crc32_ssh1, val_string_ptrlen)
FUNC2(uint, crc32_update, uint, val_string_ptrlen)
FUNC2(boolean, crcda_detect, val_string_ptrlen, val_string_ptrlen)
FUNC5(val_mpint, primegen, uint, uint, uint, val_mpint, uint)

/*
 * These functions aren't part of PuTTY's own API, but are additions
 * by testcrypt itself for administrative purposes.
 */
FUNC1(void, random_queue, val_string_ptrlen)
FUNC0(uint, random_queue_len)
FUNC0(void, random_clear)
