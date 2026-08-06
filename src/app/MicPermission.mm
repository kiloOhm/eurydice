#include "MicPermission.h"

#import <AVFoundation/AVFoundation.h>
#include <juce_events/juce_events.h>

namespace micpermission
{

State current()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:    return State::granted;
        case AVAuthorizationStatusNotDetermined: return State::undetermined;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default:                                 return State::denied;
    }
}

void request (std::function<void (bool)> callback)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL granted)
    {
        // The handler arrives on an arbitrary queue.
        juce::MessageManager::callAsync ([callback, ok = (bool) granted] { callback (ok); });
    }];
}

}
