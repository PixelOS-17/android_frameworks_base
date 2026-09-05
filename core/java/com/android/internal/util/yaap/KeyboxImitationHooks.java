/*
 * SPDX-FileCopyrightText: 2024 Paranoid Android
 * SPDX-FileCopyrightText: 2025 Neoteric OS
 * SPDX-License-Identifier: Apache-2.0
 */
package com.android.internal.util.yaap;

import android.app.ActivityThread;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.security.keymint.Algorithm;
import android.hardware.security.keymint.KeyParameter;
import android.hardware.security.keymint.KeyPurpose;
import android.hardware.security.keymint.Tag;
import android.os.Binder;
import android.security.KeyStore2;
import android.security.KeyStoreException;
import android.system.keystore2.KeyMetadata;
import android.util.Log;

import com.android.internal.util.yaap.KeyboxChainGenerator.KeyGenParameters;

import java.io.ByteArrayInputStream;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/**
 * @hide
 */
public class KeyboxImitationHooks {
    private static final String TAG = "KeyboxImitationHooks";
    private static final String UNIQUE_ID_ATTESTATION_PERMISSION =
            "android.permission.REQUEST_UNIQUE_ID_ATTESTATION";

    public static Collection<KeyParameter> prepareGenerateKeyParameters(
            Collection<KeyParameter> args) {
        if (!KeyProviderManager.isKeyboxAvailable()) {
            return args;
        }

        boolean requestedUniqueId = false;
        for (KeyParameter parameter : args) {
            if (parameter.tag == Tag.INCLUDE_UNIQUE_ID) {
                requestedUniqueId = true;
                break;
            }
        }
        if (!requestedUniqueId || hasUniqueIdAttestationPermission()) {
            return args;
        }

        List<KeyParameter> filtered = new ArrayList<>(args.size());
        for (KeyParameter parameter : args) {
            if (parameter.tag != Tag.INCLUDE_UNIQUE_ID) {
                filtered.add(parameter);
            }
        }
        Log.w(TAG, "Stripping INCLUDE_UNIQUE_ID without REQUEST_UNIQUE_ID_ATTESTATION");
        return filtered;
    }

    public static void updateCertificateChain(KeyMetadata metadata,
            Collection<KeyParameter> args) throws KeyStoreException {
        KeyMetadata certificates = new KeyMetadata();
        try {
            if (!KeyProviderManager.isKeyboxAvailable()) {
                return;
            }

            KeyGenParameters params = new KeyGenParameters(args.toArray(new KeyParameter[0]));
            if (!params.purpose.contains(KeyPurpose.SIGN)
                    || (params.algorithm != Algorithm.EC && params.algorithm != Algorithm.RSA)) {
                return;
            }

            Certificate certificate = CertificateFactory.getInstance("X.509")
                    .generateCertificate(new ByteArrayInputStream(metadata.certificate));
            List<Certificate> chain = KeyboxChainGenerator.generateCertChain(
                    Binder.getCallingUid(), certificate.getPublicKey(), params);
            if (chain == null || chain.isEmpty()) {
                return;
            }

            KeyboxUtils.putCertificateChain(certificates, chain.toArray(new Certificate[0]));
        } catch (Exception e) {
            Log.w(TAG, "Keeping backend certificates after chain preparation failed", e);
            return;
        }

        // Use the backend key ID, not an alias that may have been rebound. Propagate storage
        // failures so the caller can clean up the failed generation instead of using stale data.
        KeyStore2.getInstance().updateSubcomponents(metadata.key,
                certificates.certificate, certificates.certificateChain);
        metadata.certificate = certificates.certificate;
        metadata.certificateChain = certificates.certificateChain;
    }

    private static boolean hasUniqueIdAttestationPermission() {
        try {
            Context context = ActivityThread.currentApplication();
            if (context == null) {
                return false;
            }
            return context.checkPermission(
                    UNIQUE_ID_ATTESTATION_PERMISSION,
                    Binder.getCallingPid(),
                    Binder.getCallingUid()) == PackageManager.PERMISSION_GRANTED;
        } catch (RuntimeException e) {
            Log.w(TAG, "Unable to check REQUEST_UNIQUE_ID_ATTESTATION", e);
            return false;
        }
    }
}
