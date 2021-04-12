Tapyrus Schnorr Signature Specification
========================================

Schnorr signature in Tapyrus is adapted from the proposal by Mark B. Lundeberg from Bitcoin cash (https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-schnorr.md) which is based on the proposal by Pieter Wuille from bitcoin (bip-schnorr:https://github.com/sipa/bips/blob/bip-schnorr/bip-schnorr.mediawiki).

Signing algorithm:
-------------------
*Input:*
* The secret key sk: a 32-byte array
* The message m: a 32-byte array

To sign m for public key pubkey(sk):   
1. Let d' = int(sk).
1. Fail if d' = 0 or d' >= n, where n is the order of the elliptic curve
1. Let P = d'G.
1. Let k' = nonce_rfc6979(m, sk).
1. Let R = k'G.
1. Let k = k' if jacobi(y(R)) = 1, otherwise let k = n - k'.
1. Let e = int(sha256(bytes(x(R)) || bytes(P) || m)) mod n.
1. The signature is bytes(x(R)) || bytes((k + ed) mod n).

nonce_rfc6979(m, x):
---------------------
```
* Given the input message m and private key x

1. Process m through the hash function H, yielding:
          h1 = H(m)
2. Let V = 0x01 0x01 0x01 ... 0x01 (length of V is 32 octets).
3. Let K = 0x00 0x00 0x00 ... 0x00 (length of K is 32 octets).
4. Let K = HMAC_K(V || 0x00 || int2octets(x) || bits2octets(h1) || k')

       where '||' denotes concatenation.
       and k' (additional data) = "SCHNORR + SHA256"

5. Set V = HMAC_K(V)
6. Set K = HMAC_K(V || 0x01 || int2octets(x) || bits2octets(h1))
7. Set V = HMAC_K(V)
8. Apply the following algorithm until a proper value is found for k:

       1.  Set T to the empty sequence and the length of T, tlen = 0.
       2.  While tlen < qlen, do the following:

              V = HMAC_K(V)
              T = T || V

       3.  Compute k = bits2int(T)

           If k is within [1,q-1] return k

           Otherwise, compute:
              K = HMAC_K(V || 0x00)
              V = HMAC_K(V)
           and loop (try to generate a new T, and so on).
```

Verification:
-------------
*Input:*
* The public key P: a 32-byte array
* The message m: a 32-byte array
* A signature (r,s): a 64-byte array

1. Fail if point P is not actually on the curve, or if it is the point at infinity.   
1. Fail if r >= p, where p is the field size used in secp256k1.   
1. Fail if s >= n, where n is the order of the secp256k1 curve.   
1. Let BP be the 33-byte encoding of P as a compressed point.   
1. Let Br be the 32-byte encoding of r as an unsigned big-endian 256-bit integer.   
1. Let e = H(Br | BP | m) mod n.    
1. Here | means byte-string concatenation and function H() takes the SHA256 hash of its 97-byte input and returns it decoded as a big-endian unsigned integer.   
1. Let R' = sG - eP, where G is the secp256k1 generator point.   
1. Fail if R' is the point at infinity.   
1. Fail if the X coordinate of R' is not equal to r.   
1. Fail if the Jacobi symbol of the Y coordinate of R' is not 1.   
1. Otherwise, the signature is valid.   

Signature Encoding:
-------------------
In Tapyrus, Schnorr signatures are encoded in fixed size as

*| r | s | hashbyte |*  

_r_ - 32 bytes. the unsigned big-endian 256-bit encoding of the Schnorr signature's r integer.    
_s_ - 32 bytes. the unsigned big-endian 256-bit encoding of the Schnorr signature's s integer.   
_hashtype_ - 1 byte.  informs OP_CHECKSIG/VERIFY mechanics.

Signing on Tapyrus can be done using either ECDSA or Schnorr signature schemes. The following RPCs have an additional parameter to choose the signature scheme:
* signrawtransaction
* signrawtransactionwithkey
* signrawtransactionwithwallet

Verification of a script with Schnorr signature is done using the following OP codes:
* OP_CHECKSIG(VERIFY)
* OP_CHECKDATASIG(VERIFY)
* OP_MULTISIG(VERIFY)

Note: 
* Tapyrus assumes that all 65 byte signatures use Schnorr scheme(64 bytes in case of OP_CHECKDATASIG).
* OP_MULTISIG is similar in both ECDSA and Schnorr schemes, we do not use batch verification in schnorr signatures.
* OP_MULTISIG does not allow mixing ECDSA and Schnorr signatures. 
