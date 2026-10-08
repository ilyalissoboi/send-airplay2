// SPDX-License-Identifier: Apache-2.0
using System;
using Windows.Security.Credentials;

namespace SendAirPlay2.UwpHost
{
    /// <summary>
    /// The D49 host store for a UWP app: one PasswordVault credential per profile,
    /// resource <see cref="Resource"/>, user name = profile, password = the record
    /// as Base64 (the locker holds strings).
    /// </summary>
    /// <remarks>
    /// The locker is this package's own (AppContainer isolation), holds at most 20
    /// credentials per app, and can roam with the user's Microsoft account; see
    /// docs/credential-interface.md. Base64 strings cannot be wiped from managed
    /// memory; this test host accepts that.
    /// </remarks>
    internal sealed class PasswordVaultStore : ICredentialStore
    {
        internal const string Resource = "send-airplay2";

        // HRESULT_FROM_WIN32(ERROR_NOT_FOUND): Retrieve/FindAll for a missing entry.
        private const int ElementNotFound = unchecked((int)0x80070490);

        private readonly PasswordVault vault = new PasswordVault();

        public CredentialStoreResult Load(string profile, byte[] buffer, out int length)
        {
            length = 0;
            var credential = Find(profile);
            if (credential == null)
            {
                return CredentialStoreResult.Absent;
            }
            credential.RetrievePassword();
            var record = Convert.FromBase64String(credential.Password);
            try
            {
                if (record.Length > buffer.Length)
                {
                    return CredentialStoreResult.Unavailable;
                }
                Array.Copy(record, buffer, record.Length);
                length = record.Length;
                return CredentialStoreResult.Ok;
            }
            finally
            {
                Array.Clear(record, 0, record.Length);
            }
        }

        public CredentialStoreResult SaveNew(string profile, byte[] record)
        {
            if (Find(profile) != null)
            {
                return CredentialStoreResult.Exists;
            }
            vault.Add(new PasswordCredential(Resource, profile, Convert.ToBase64String(record)));
            return CredentialStoreResult.Ok;
        }

        public CredentialStoreResult Erase(string profile)
        {
            var credential = Find(profile);
            if (credential == null)
            {
                return CredentialStoreResult.Absent;
            }
            vault.Remove(credential);
            return CredentialStoreResult.Ok;
        }

        /// <summary>Number of credentials this package holds under the resource.</summary>
        internal int Count()
        {
            try
            {
                return vault.FindAllByResource(Resource).Count;
            }
            catch (Exception error) when (error.HResult == ElementNotFound)
            {
                return 0;
            }
        }

        private PasswordCredential? Find(string profile)
        {
            try
            {
                return vault.Retrieve(Resource, profile);
            }
            catch (Exception error) when (error.HResult == ElementNotFound)
            {
                return null;
            }
        }
    }
}
