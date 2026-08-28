#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>

class NullCPUPowerManagement : public IOService {
    OSDeclareDefaultStructors(NullCPUPowerManagement)
public:
    virtual bool init(OSDictionary* dictionary = 0) override;
    virtual IOService* probe(IOService* provider, SInt32* score) override;
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    virtual void free(void) override;
};

OSDefineMetaClassAndStructors(NullCPUPowerManagement, IOService)

bool NullCPUPowerManagement::init(OSDictionary* dict) {
    bool res = super::init(dict);
    IOLog("NullCPUPowerManagement::init\n");
    return res;
}

IOService* NullCPUPowerManagement::probe(IOService* provider, SInt32* score) {
    IOService* res = super::probe(provider, score);
    IOLog("NullCPUPowerManagement::probe\n");
    return res;
}

bool NullCPUPowerManagement::start(IOService* provider) {
    bool res = super::start(provider);
    IOLog("NullCPUPowerManagement::start\n");
    return res;
}

void NullCPUPowerManagement::stop(IOService* provider) {
    IOLog("NullCPUPowerManagement::stop\n");
    super::stop(provider);
}

void NullCPUPowerManagement::free(void) {
    IOLog("NullCPUPowerManagement::free\n");
    super::free();
}

extern "C" {
    kern_return_t NullCPUPowerManagement_start(kmod_info_t* ki, void* d) {
        IOLog("NullCPUPowerManagement kmod start\n");
        return KERN_SUCCESS;
    }

    kern_return_t NullCPUPowerManagement_stop(kmod_info_t* ki, void* d) {
        IOLog("NullCPUPowerManagement kmod stop\n");
        return KERN_SUCCESS;
    }
}
