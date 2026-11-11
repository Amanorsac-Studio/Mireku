// Licence.cpp — the one translation unit that includes the licence client
// (and with it <windows.h>). See Licence.h.
#include "Licence.h"
#include "LicenseClient.h"

#include <mutex>

namespace jm::licence {

namespace
{
    Result toResult (const amanorsacstudio::LicenseResult& r)
    {
        Result out;
        out.ok = r.ok;
        out.code = r.code;
        out.message = r.message;
        return out;
    }

    class ClientService final : public Service
    {
    public:
        bool isLicensed() const noexcept override { return client_.isLicensed(); }

        Info info() const override
        {
            Info i;
            i.licensed    = client_.isLicensed();
            i.key         = client_.getLicenseKey();
            i.hasKey      = i.key.isNotEmpty();
            i.offlineDays = juce::jmax (0, client_.daysOfOfflineAccessRemaining());
            i.connectSoon = client_.isOfflineProofExpiring();
            return i;
        }

        void activate (const juce::String& key, std::function<void (Result)> done) override
        {
            client_.activate (key, [done = std::move (done)] (amanorsacstudio::LicenseResult r)
            {
                if (done) done (toResult (r));
            });
        }

        void deactivate (std::function<void (Result)> done) override
        {
            client_.deactivateThisDevice ([done = std::move (done)] (amanorsacstudio::LicenseResult r)
            {
                if (done) done (toResult (r));
            });
        }

    private:
        amanorsacstudio::LicenseClient client_;
    };
}

std::shared_ptr<Service> Service::shared()
{
    static std::mutex lock;
    static std::weak_ptr<Service> held;

    const std::lock_guard<std::mutex> guard (lock);
    if (auto existing = held.lock())
        return existing;
    auto made = std::make_shared<ClientService>();
    held = made;
    return made;
}

//==============================================================================
ProofVerdict evaluateProof (const juce::String& proof, const juce::String& deviceKey,
                            const juce::String& licenceKey, juce::int64 nowMs,
                            const juce::uint8* publicKey65)
{
    const auto c = amanorsacstudio::checkProof (proof, deviceKey, licenceKey, publicKey65);
    ProofVerdict v;
    v.signatureOk = c.signatureOk;
    v.bound       = c.bound;
    v.licensed    = c.licensedAt (nowMs);
    v.issuedAt    = c.issuedAt;
    v.expiresAt   = c.expiresAt;
    v.graceUntil  = c.graceUntil;
    return v;
}

bool secretStoreRoundTrips()
{
    namespace lc = amanorsacstudio::licensecrypto;
    if (! lc::secretStorageIsEncrypted())
        return false;

    const juce::String secret ("TEST-610E-6070-3493");
    const juce::MemoryBlock plain (secret.toRawUTF8(), (size_t) secret.getNumBytesAsUTF8());
    const auto stored = lc::protectSecret (plain);
    if (stored.getSize() == 0 || stored == plain)
        return false;
    // the stored form must not carry the plain text
    if (stored.toString().contains (secret))
        return false;
    return lc::unprotectSecret (stored) == plain;
}

juce::String studioKeyFingerprint()
{
    return juce::String::toHexString (amanorsacstudio::kLicenseSigningKey + 1, 8, 0);
}

} // namespace jm::licence
