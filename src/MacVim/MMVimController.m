/* vi:set ts=8 sts=4 sw=4 ft=objc:
 *
 * VIM - Vi IMproved		by Bram Moolenaar
 *				MacVim GUI port by Bjorn Winckler
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */
/*
 * MMVimController
 *
 * Coordinates input/output to/from backend.  Each MMBackend communicates
 * directly with a MMVimController.
 *
 * MMVimController does not deal with visual presentation.  Essentially it
 * should be able to run with no window present.
 *
 * Output from the backend is received in processCommandQueue:.  Input is sent
 * to the backend via sendMessage:data: or addVimInput:.  The latter allows
 * execution of arbitrary strings in the Vim process, much like the Vim script
 * function remote_send() does.  The messages that may be passed between
 * frontend and backend are defined in an enum in MacVim.h.
 */

#import "MMAppController.h"
#import "MMAtsuiTextView.h"
#import "MMFindReplaceController.h"
#import "MMTextView.h"
#import "MMVimController.h"
#import "MMVimView.h"
#import "MMWindowController.h"
#import "Miscellaneous.h"

#ifdef MM_ENABLE_PLUGINS
#import "MMPlugInManager.h"
#endif

static NSString *MMDefaultToolbarImageName = @"Attention";
static int MMAlertTextFieldHeight = 22;

// NOTE: By default a message sent to the backend will be dropped if it cannot
// be delivered instantly; otherwise there is a possibility that MacVim will
// 'beachball' while waiting to deliver DO messages to an unresponsive Vim
// process.  This means that you cannot rely on any message sent with
// sendMessage: to actually reach Vim.
static NSTimeInterval MMBackendProxyRequestTimeout = 0;

// Timeout used for setDialogReturn:.
static NSTimeInterval MMSetDialogReturnTimeout = 1.0;

// Maximum number of items in the receiveQueue.  (It is hard to predict what
// consequences changing this number will have.)
static int MMReceiveQueueCap = 100;

static BOOL isUnsafeMessage(int msgid);


@interface MMAlert : NSAlert {
    NSTextField *textField;
}
- (void)setTextFieldString:(NSString *)textFieldString;
- (NSTextField *)textField;
@end


@interface MMVimController (Private)
- (void)doProcessCommandQueue:(NSArray *)queue;
- (void)handleMessage:(int)msgid data:(NSData *)data;
- (void)savePanelDidEnd:(NSSavePanel *)panel code:(int)code
                context:(void *)context;
- (void)alertDidEnd:(MMAlert *)alert code:(int)code context:(void *)context;
- (NSMenuItem *)menuItemForDescriptor:(NSArray *)desc;
- (NSMenu *)parentMenuForDescriptor:(NSArray *)desc;
- (NSMenu *)topLevelMenuForTitle:(NSString *)title;
- (void)addMenuWithDescriptor:(NSArray *)desc atIndex:(int)index;
- (void)addMenuItemWithDescriptor:(NSArray *)desc
                          atIndex:(int)index
                              tip:(NSString *)tip
                             icon:(NSString *)icon
                    keyEquivalent:(NSString *)keyEquivalent
                     modifierMask:(int)modifierMask
                           action:(NSString *)action
                      isAlternate:(BOOL)isAlternate;
- (void)removeMenuItemWithDescriptor:(NSArray *)desc;
- (void)enableMenuItemWithDescriptor:(NSArray *)desc state:(BOOL)on;
- (void)addToolbarItemToDictionaryWithLabel:(NSString *)title
        toolTip:(NSString *)tip icon:(NSString *)icon;
- (void)addToolbarItemWithLabel:(NSString *)label
                          tip:(NSString *)tip icon:(NSString *)icon
                      atIndex:(int)idx;
- (void)popupMenuWithDescriptor:(NSArray *)desc
                          atRow:(NSNumber *)row
                         column:(NSNumber *)col;
- (void)popupMenuWithAttributes:(NSDictionary *)attrs;
- (void)connectionDidDie:(NSNotification *)notification;
- (void)scheduleClose;
@end




@implementation MMVimController

- (id)initWithBackend:(id)backend pid:(int)processIdentifier
{
    if (!(self = [super init]))
        return nil;

    windowController =
        [[MMWindowController alloc] initWithVimController:self];
    backendProxy = [backend retain];
    sendQueue = [NSMutableArray new];
    receiveQueue = [NSMutableArray new];
    popupMenuItems = [[NSMutableArray alloc] init];
    toolbarItemDict = [[NSMutableDictionary alloc] init];
    pid = processIdentifier;
    creationDate = [[NSDate alloc] init];

    NSConnection *connection = [backendProxy connectionForProxy];

    // TODO: Check that this will not set the timeout for the root proxy
    // (in MMAppController).
    [connection setRequestTimeout:MMBackendProxyRequestTimeout];

    [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(connectionDidDie:)
                name:NSConnectionDidDieNotification object:connection];

    // Set up a main menu with only a "MacVim" menu (copied from a template
    // which itself is set up in MainMenu.nib).  The main menu is populated
    // by Vim later on.
    mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];
    NSMenuItem *appMenuItem = [[MMAppController sharedInstance]
                                        appMenuItemTemplate];
    appMenuItem = [[appMenuItem copy] autorelease];

    // Note: If the title of the application menu is anything but what
    // CFBundleName says then the application menu will not be typeset in
    // boldface for some reason.  (It should already be set when we copy
    // from the default main menu, but this is not the case for some
    // reason.)
    NSString *appName = [[NSBundle mainBundle]
            objectForInfoDictionaryKey:@"CFBundleName"];
    [appMenuItem setTitle:appName];

    [mainMenu addItem:appMenuItem];

#ifdef MM_ENABLE_PLUGINS
    instanceMediator = [[MMPlugInInstanceMediator alloc]
            initWithVimController:self];
#endif

    isInitialized = YES;

    return self;
}

- (void)dealloc
{
    LOG_DEALLOC

    isInitialized = NO;

#ifdef MM_ENABLE_PLUGINS
    [instanceMediator release]; instanceMediator = nil;
#endif

    [serverName release];  serverName = nil;
    [backendProxy release];  backendProxy = nil;
    [sendQueue release];  sendQueue = nil;
    [receiveQueue release];  receiveQueue = nil;

    [toolbarItemDict release];  toolbarItemDict = nil;
    [toolbar release];  toolbar = nil;
    [popupMenuItems release];  popupMenuItems = nil;
    [windowController release];  windowController = nil;

    [vimState release];  vimState = nil;
    [mainMenu release];  mainMenu = nil;
    [creationDate release];  creationDate = nil;

    [super dealloc];
}

- (MMWindowController *)windowController
{
    return windowController;
}

#ifdef MM_ENABLE_PLUGINS
- (MMPlugInInstanceMediator *)instanceMediator
{
    return instanceMediator;
}
#endif

- (NSDictionary *)vimState
{
    return vimState;
}

- (id)objectForVimStateKey:(NSString *)key
{
    return [vimState objectForKey:key];
}

- (NSMenu *)mainMenu
{
    return mainMenu;
}

- (BOOL)isPreloading
{
    return isPreloading;
}

- (void)setIsPreloading:(BOOL)yn
{
    isPreloading = yn;
}

- (NSDate *)creationDate
{
    return creationDate;
}

- (void)setServerName:(NSString *)name
{
    if (name != serverName) {
        [serverName release];
        serverName = [name copy];
    }
}

- (NSString *)serverName
{
    return serverName;
}

- (int)pid
{
    return pid;
}

- (void)dropFiles:(NSArray *)filenames forceOpen:(BOOL)force
{
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];

    // Default to opening in tabs if layout is invalid or set to "windows".
    int layout = [ud integerForKey:MMOpenLayoutKey];
    if (layout < 0 || layout > MMLayoutTabs)
        layout = MMLayoutTabs;

    BOOL splitVert = [ud boolForKey:MMVerticalSplitKey];
    if (splitVert && MMLayoutHorizontalSplit == layout)
        layout = MMLayoutVerticalSplit;

    NSDictionary *args = [NSDictionary dictionaryWithObjectsAndKeys:
            [NSNumber numberWithInt:layout],    @"layout",
            filenames,                          @"filenames",
            [NSNumber numberWithBool:force],    @"forceOpen",
            nil];

    [self sendMessage:DropFilesMsgID data:[args dictionaryAsData]];
}

- (void)file:(NSString *)filename draggedToTabAtIndex:(NSUInteger)tabIndex
{
    NSString *fnEsc = [filename stringByEscapingSpecialFilenameCharacters];
    NSString *input = [NSString stringWithFormat:@"<C-\\><C-N>:silent "
                       "tabnext %d |"
                       "edit! %@<CR>", tabIndex + 1, fnEsc];
    [self addVimInput:input];
}

- (void)filesDraggedToTabBar:(NSArray *)filenames
{
    NSUInteger i, count = [filenames count];
    NSMutableString *input = [NSMutableString stringWithString:@"<C-\\><C-N>"
                              ":silent! tabnext 9999"];
    for (i = 0; i < count; i++) {
        NSString *fn = [filenames objectAtIndex:i];
        NSString *fnEsc = [fn stringByEscapingSpecialFilenameCharacters];
        [input appendFormat:@"|tabedit %@", fnEsc];
    }
    [input appendString:@"<CR>"];
    [self addVimInput:input];
}

- (void)dropString:(NSString *)string
{
    int len = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1;
    if (len > 0) {
        NSMutableData *data = [NSMutableData data];

        [data appendBytes:&len length:sizeof(int)];
        [data appendBytes:[string UTF8String] length:len];

        [self sendMessage:DropStringMsgID data:data];
    }
}

- (void)passArguments:(NSDictionary *)args
{
    if (!args) return;

    [self sendMessage:OpenWithArgumentsMsgID data:[args dictionaryAsData]];

    // HACK! Fool findUnusedEditor into thinking that this controller is not
    // unused anymore, in case it is called before the arguments have reached
    // the Vim process.  This should be a "safe" hack since the next time the
    // Vim process flushes its output queue the state will be updated again (at
    // which time the "unusedEditor" state will have been properly set).
    NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithDictionary:
            vimState];
    [dict setObject:[NSNumber numberWithBool:NO] forKey:@"unusedEditor"];
    [vimState release];
    vimState = [dict copy];
}

- (void)sendMessage:(int)msgid data:(NSData *)data
{
    //NSLog(@"sendMessage:%s (isInitialized=%d inProcessCommandQueue=%d)",
    //        MessageStrings[msgid], isInitialized, inProcessCommandQueue);

    if (!isInitialized) return;

    if (inProcessCommandQueue) {
        //NSLog(@"In process command queue; delaying message send.");
        [sendQueue addObject:[NSNumber numberWithInt:msgid]];
        if (data)
            [sendQueue addObject:data];
        else
            [sendQueue addObject:[NSNull null]];
        return;
    }

    @try {
        [backendProxy processInput:msgid data:data];
    }
    @catch (NSException *e) {
        //NSLog(@"%@ %s Exception caught during DO call: %@",
        //        [self className], _cmd, e);
    }
}

- (BOOL)sendMessageNow:(int)msgid data:(NSData *)data
               timeout:(NSTimeInterval)timeout
{
    // Send a message with a timeout.  USE WITH EXTREME CAUTION!  Sending
    // messages in rapid succession with a timeout may cause MacVim to beach
    // ball forever.  In almost all circumstances sendMessage:data: should be
    // used instead.

    if (!isInitialized || inProcessCommandQueue)
        return NO;

    if (timeout < 0) timeout = 0;

    BOOL sendOk = YES;
    NSConnection *conn = [backendProxy connectionForProxy];
    NSTimeInterval oldTimeout = [conn requestTimeout];

    [conn setRequestTimeout:timeout];

    @try {
        [backendProxy processInput:msgid data:data];
    }
    @catch (NSException *e) {
        sendOk = NO;
    }
    @finally {
        [conn setRequestTimeout:oldTimeout];
    }

    return sendOk;
}

- (void)addVimInput:(NSString *)string
{
    // This is a very general method of adding input to the Vim process.  It is
    // basically the same as calling remote_send() on the process (see
    // ':h remote_send').
    if (string) {
        NSData *data = [string dataUsingEncoding:NSUTF8StringEncoding];
        [self sendMessage:AddInputMsgID data:data];
    }
}

- (NSString *)evaluateVimExpression:(NSString *)expr
{
    NSString *eval = nil;

    @try {
        eval = [backendProxy evaluateExpression:expr];
    }
    @catch (NSException *ex) { /* do nothing */ }

    return eval;
}

- (id)evaluateVimExpressionCocoa:(NSString *)expr errorString:(NSString **)errstr
{
    id eval = nil;

    @try {
        eval = [backendProxy evaluateExpressionCocoa:expr
                                         errorString:errstr];
    } @catch (NSException *ex) {
        *errstr = [ex reason];
    }

    return eval;
}

- (id)backendProxy
{
    return backendProxy;
}

- (void)cleanup
{
    if (!isInitialized) return;

    isInitialized = NO;
    [toolbar setDelegate:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    //[[backendProxy connectionForProxy] invalidate];
    //[windowController close];
    [windowController cleanup];
}

- (oneway void)showSavePanelWithAttributes:(in bycopy NSDictionary *)attr
{
    if (!isInitialized) return;

    BOOL inDefaultMode = [[[NSRunLoop currentRunLoop] currentMode]
                                        isEqual:NSDefaultRunLoopMode];
    if (!inDefaultMode) {
        // Delay call until run loop is in default mode.
        [self performSelectorOnMainThread:
                                        @selector(showSavePanelWithAttributes:)
                               withObject:attr
			    waitUntilDone:NO
			            modes:[NSArray arrayWithObject:
                                           NSDefaultRunLoopMode]];
        return;
    }

    NSString *dir = [attr objectForKey:@"dir"];
    BOOL saving = [[attr objectForKey:@"saving"] boolValue];

    if (!dir) {
        // 'dir == nil' means: set dir to the pwd of the Vim process, or let
        // open dialog decide (depending on the below user default).
        BOOL trackPwd = [[NSUserDefaults standardUserDefaults]
                boolForKey:MMDialogsTrackPwdKey];
        if (trackPwd)
            dir = [vimState objectForKey:@"pwd"];
    }

    if (saving) {
        [[NSSavePanel savePanel] beginSheetForDirectory:dir file:nil
                modalForWindow:[windowController window]
                 modalDelegate:self
                didEndSelector:@selector(savePanelDidEnd:code:context:)
                   contextInfo:NULL];
    } else {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setAccessoryView:openPanelAccessoryView()];

        [panel beginSheetForDirectory:dir file:nil types:nil
                modalForWindow:[windowController window]
                 modalDelegate:self
                didEndSelector:@selector(savePanelDidEnd:code:context:)
                   contextInfo:NULL];
    }
}

- (oneway void)presentDialogWithAttributes:(in bycopy NSDictionary *)attr
{
    if (!isInitialized) return;

    BOOL inDefaultMode = [[[NSRunLoop currentRunLoop] currentMode]
                                        isEqual:NSDefaultRunLoopMode];
    if (!inDefaultMode) {
        // Delay call until run loop is in default mode.
        [self performSelectorOnMainThread:
                                        @selector(presentDialogWithAttributes:)
                               withObject:attr
			    waitUntilDone:NO
			            modes:[NSArray arrayWithObject:
                                           NSDefaultRunLoopMode]];
        return;
    }

    NSArray *buttonTitles = [attr objectForKey:@"buttonTitles"];
    if (!(buttonTitles && [buttonTitles count])) return;

    int style = [[attr objectForKey:@"alertStyle"] intValue];
    NSString *message = [attr objectForKey:@"messageText"];
    NSString *text = [attr objectForKey:@"informativeText"];
    NSString *textFieldString = [attr objectForKey:@"textFieldString"];
    MMAlert *alert = [[MMAlert alloc] init];

    // NOTE! This has to be done before setting the informative text.
    if (textFieldString)
        [alert setTextFieldString:textFieldString];

    [alert setAlertStyle:style];

    if (message) {
        [alert setMessageText:message];
    } else {
        // If no message text is specified 'Alert' is used, which we don't
        // want, so set an empty string as message text.
        [alert setMessageText:@""];
    }

    if (text) {
        [alert setInformativeText:text];
    } else if (textFieldString) {
        // Make sure there is always room for the input text field.
        [alert setInformativeText:@""];
    }

    unsigned i, count = [buttonTitles count];
    for (i = 0; i < count; ++i) {
        NSString *title = [buttonTitles objectAtIndex:i];
        // NOTE: The title of the button may contain the character '&' to
        // indicate that the following letter should be the key equivalent
        // associated with the button.  Extract this letter and lowercase it.
        NSString *keyEquivalent = nil;
        NSRange hotkeyRange = [title rangeOfString:@"&"];
        if (NSNotFound != hotkeyRange.location) {
            if ([title length] > NSMaxRange(hotkeyRange)) {
                NSRange keyEquivRange = NSMakeRange(hotkeyRange.location+1, 1);
                keyEquivalent = [[title substringWithRange:keyEquivRange]
                    lowercaseString];
            }

            NSMutableString *string = [NSMutableString stringWithString:title];
            [string deleteCharactersInRange:hotkeyRange];
            title = string;
        }

        [alert addButtonWithTitle:title];

        // Set key equivalent for the button, but only if NSAlert hasn't
        // already done so.  (Check the documentation for
        // - [NSAlert addButtonWithTitle:] to see what key equivalents are
        // automatically assigned.)
        NSButton *btn = [[alert buttons] lastObject];
        if ([[btn keyEquivalent] length] == 0 && keyEquivalent) {
            [btn setKeyEquivalent:keyEquivalent];
        }
    }

    [alert beginSheetModalForWindow:[windowController window]
                      modalDelegate:self
                     didEndSelector:@selector(alertDidEnd:code:context:)
                        contextInfo:NULL];

    [alert release];
}

- (oneway void)processCommandQueue:(in bycopy NSArray *)queue
{
    if (!isInitialized) return;

    if (inProcessCommandQueue) {
        // NOTE!  If a synchronous DO call is made during
        // doProcessCommandQueue: below it may happen that this method is
        // called a second time while the synchronous message is waiting for a
        // reply (could also happen if doProcessCommandQueue: enters a modal
        // loop, see comment below).  Since this method cannot be considered
        // reentrant, we queue the input and return immediately.
        //
        // If doProcessCommandQueue: enters a modal loop (happens e.g. on
        // ShowPopupMenuMsgID) then the receiveQueue could grow to become
        // arbitrarily large because DO calls still get processed.  To avoid
        // this we set a cap on the size of the queue and simply clear it if it
        // becomes too large.  (That is messages will be dropped and hence Vim
        // and MacVim will at least temporarily be out of sync.)
        if ([receiveQueue count] >= MMReceiveQueueCap)
            [receiveQueue removeAllObjects];

        [receiveQueue addObject:queue];
        return;
    }

    inProcessCommandQueue = YES;
    [self doProcessCommandQueue:queue];

    int i;
    for (i = 0; i < [receiveQueue count]; ++i) {
        // Note that doProcessCommandQueue: may cause the receiveQueue to grow
        // or get cleared (due to cap being hit).  Make sure to retain the item
        // to process or it may get released from under us.
        NSArray *q = [[receiveQueue objectAtIndex:i] retain];
        [self doProcessCommandQueue:q];
        [q release];
    }

    // We assume that the remaining calls make no synchronous DO calls.  If
    // that did happen anyway, the command queue could get processed out of
    // order.

    // See comment below why this is called here and not later.
    [windowController processCommandQueueDidFinish];

    // NOTE: Ensure that no calls are made after this "if" clause that may call
    // sendMessage::.  If this happens anyway, such messages will be put on the
    // send queue and then the queue will not be flushed until the next time
    // this method is called.
    if ([sendQueue count] > 0) {
        @try {
            [backendProxy processInputAndData:sendQueue];
        }
        @catch (NSException *e) {
            // Connection timed out, just ignore this.
            //NSLog(@"WARNING! Connection timed out in %s", _cmd);
        }

        [sendQueue removeAllObjects];
    }

    [receiveQueue removeAllObjects];
    inProcessCommandQueue = NO;
}

- (NSToolbarItem *)toolbar:(NSToolbar *)theToolbar
    itemForItemIdentifier:(NSString *)itemId
    willBeInsertedIntoToolbar:(BOOL)flag
{
    NSToolbarItem *item = [toolbarItemDict objectForKey:itemId];
    if (!item) {
        NSLog(@"WARNING:  No toolbar item with id '%@'", itemId);
    }

    return item;
}

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)theToolbar
{
    return nil;
}

- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)theToolbar
{
    return nil;
}

@end // MMVimController



@implementation MMVimController (Private)

- (void)doProcessCommandQueue:(NSArray *)queue
{
    NSMutableArray *delayQueue = nil;

    @try {
        unsigned i, count = [queue count];
        if (count % 2) {
            NSLog(@"WARNING: Uneven number of components (%d) in command "
                    "queue.  Skipping...", count);
            return;
        }

        //NSLog(@"======== %s BEGIN ========", _cmd);
        for (i = 0; i < count; i += 2) {
            NSData *value = [queue objectAtIndex:i];
            NSData *data = [queue objectAtIndex:i+1];

            int msgid = *((int*)[value bytes]);
            //NSLog(@"%s%s", _cmd, MessageStrings[msgid]);

            BOOL inDefaultMode = [[[NSRunLoop currentRunLoop] currentMode]
                                                isEqual:NSDefaultRunLoopMode];
            if (!inDefaultMode && isUnsafeMessage(msgid)) {
                // NOTE: Because we may be listening to DO messages in "event
                // tracking mode" we have to take extra care when doing things
                // like releasing view items (and other Cocoa objects).
                // Messages that may be potentially "unsafe" are delayed until
                // the run loop is back to default mode at which time they are
                // safe to call again.
                //   A problem with this approach is that it is hard to
                // classify which messages are unsafe.  As a rule of thumb, if
                // a message may release an object used by the Cocoa framework
                // (e.g. views) then the message should be considered unsafe.
                //   Delaying messages may have undesired side-effects since it
                // means that messages may not be processed in the order Vim
                // sent them, so beware.
                if (!delayQueue)
                    delayQueue = [NSMutableArray array];

                //NSLog(@"Adding unsafe message '%s' to delay queue (mode=%@)",
                //        MessageStrings[msgid],
                //        [[NSRunLoop currentRunLoop] currentMode]);
                [delayQueue addObject:value];
                [delayQueue addObject:data];
            } else {
                [self handleMessage:msgid data:data];
            }
        }
        //NSLog(@"======== %s  END  ========", _cmd);
    }
    @catch (NSException *e) {
        NSLog(@"Exception caught whilst processing command queue: %@", e);
    }

    if (delayQueue) {
        //NSLog(@"    Flushing delay queue (%d items)", [delayQueue count]/2);
        [self performSelectorOnMainThread:@selector(processCommandQueue:)
                               withObject:delayQueue
			    waitUntilDone:NO
			            modes:[NSArray arrayWithObject:
                                           NSDefaultRunLoopMode]];
    }
}

- (void)handleMessage:(int)msgid data:(NSData *)data
{
    //if (msgid != AddMenuMsgID && msgid != AddMenuItemMsgID &&
    //        msgid != EnableMenuItemMsgID)
    //    NSLog(@"%@ %s%s", [self className], _cmd, MessageStrings[msgid]);

    if (OpenWindowMsgID == msgid) {
        [windowController openWindow];

        // If the vim controller is preloading then the window will be
        // displayed when it is taken off the preload cache.
        if (!isPreloading)
            [windowController showWindow];
    } else if (BatchDrawMsgID == msgid) {
        [[[windowController vimView] textView] performBatchDrawWithData:data];
    } else if (SelectTabMsgID == msgid) {
#if 0   // NOTE: Tab selection is done inside updateTabsWithData:.
        const void *bytes = [data bytes];
        int idx = *((int*)bytes);
        //NSLog(@"Selecting tab with index %d", idx);
        [windowController selectTabWithIndex:idx];
#endif
    } else if (UpdateTabBarMsgID == msgid) {
        [windowController updateTabsWithData:data];
    } else if (ShowTabBarMsgID == msgid) {
        [windowController showTabBar:YES];
    } else if (HideTabBarMsgID == msgid) {
        [windowController showTabBar:NO];
    } else if (SetTextDimensionsMsgID == msgid || LiveResizeMsgID == msgid ||
            SetTextDimensionsReplyMsgID == msgid) {
        const void *bytes = [data bytes];
        int rows = *((int*)bytes);  bytes += sizeof(int);
        int cols = *((int*)bytes);  bytes += sizeof(int);

        [windowController setTextDimensionsWithRows:rows
                                 columns:cols
                                  isLive:(LiveResizeMsgID==msgid)
                                 isReply:(SetTextDimensionsReplyMsgID==msgid)];
    } else if (SetWindowTitleMsgID == msgid) {
        const void *bytes = [data bytes];
        int len = *((int*)bytes);  bytes += sizeof(int);

        NSString *string = [[NSString alloc] initWithBytes:(void*)bytes
                length:len encoding:NSUTF8StringEncoding];

        // While in live resize the window title displays the dimensions of the
        // window so don't clobber this with a spurious "set title" message
        // from Vim.
        if (![[windowController vimView] inLiveResize])
            [windowController setTitle:string];

        [string release];
    } else if (SetDocumentFilenameMsgID == msgid) {
        const void *bytes = [data bytes];
        int len = *((int*)bytes);  bytes += sizeof(int);

        if (len > 0) {
            NSString *filename = [[NSString alloc] initWithBytes:(void*)bytes
                    length:len encoding:NSUTF8StringEncoding];

            [windowController setDocumentFilename:filename];

            [filename release];
        } else {
            [windowController setDocumentFilename:@""];
        }
    } else if (AddMenuMsgID == msgid) {
        NSDictionary *attrs = [NSDictionary dictionaryWithData:data];
        [self addMenuWithDescriptor:[attrs objectForKey:@"descriptor"]
                atIndex:[[attrs objectForKey:@"index"] intValue]];
    } else if (AddMenuItemMsgID == msgid) {
        NSDictionary *attrs = [NSDictionary dictionaryWithData:data];
        [self addMenuItemWithDescriptor:[attrs objectForKey:@"descriptor"]
                      atIndex:[[attrs objectForKey:@"index"] intValue]
                          tip:[attrs objectForKey:@"tip"]
                         icon:[attrs objectForKey:@"icon"]
                keyEquivalent:[attrs objectForKey:@"keyEquivalent"]
                 modifierMask:[[attrs objectForKey:@"modifierMask"] intValue]
                       action:[attrs objectForKey:@"action"]
                  isAlternate:[[attrs objectForKey:@"isAlternate"] boolValue]];
    } else if (RemoveMenuItemMsgID == msgid) {
        NSDictionary *attrs = [NSDictionary dictionaryWithData:data];
        [self removeMenuItemWithDescriptor:[attrs objectForKey:@"descriptor"]];
    } else if (EnableMenuItemMsgID == msgid) {
        NSDictionary *attrs = [NSDictionary dictionaryWithData:data];
        [self enableMenuItemWithDescriptor:[attrs objectForKey:@"descriptor"]
                state:[[attrs objectForKey:@"enable"] boolValue]];
    } else if (ShowToolbarMsgID == msgid) {
        const void *bytes = [data bytes];
        int enable = *((int*)bytes);  bytes += sizeof(int);
        int flags = *((int*)bytes);  bytes += sizeof(int);

        int mode = NSToolbarDisplayModeDefault;
        if (flags & ToolbarLabelFlag) {
            mode = flags & ToolbarIconFlag ? NSToolbarDisplayModeIconAndLabel
                    : NSToolbarDisplayModeLabelOnly;
        } else if (flags & ToolbarIconFlag) {
            mode = NSToolbarDisplayModeIconOnly;
        }

        int size = flags & ToolbarSizeRegularFlag ? NSToolbarSizeModeRegular
                : NSToolbarSizeModeSmall;

        [windowController showToolbar:enable size:size mode:mode];
    } else if (CreateScrollbarMsgID == msgid) {
        const void *bytes = [data bytes];
        long ident = *((long*)bytes);  bytes += sizeof(long);
        int type = *((int*)bytes);  bytes += sizeof(int);

        [windowController createScrollbarWithIdentifier:ident type:type];
    } else if (DestroyScrollbarMsgID == msgid) {
        const void *bytes = [data bytes];
        long ident = *((long*)bytes);  bytes += sizeof(long);

        [windowController destroyScrollbarWithIdentifier:ident];
    } else if (ShowScrollbarMsgID == msgid) {
        const void *bytes = [data bytes];
        long ident = *((long*)bytes);  bytes += sizeof(long);
        int visible = *((int*)bytes);  bytes += sizeof(int);

        [windowController showScrollbarWithIdentifier:ident state:visible];
    } else if (SetScrollbarPositionMsgID == msgid) {
        const void *bytes = [data bytes];
        long ident = *((long*)bytes);  bytes += sizeof(long);
        int pos = *((int*)bytes);  bytes += sizeof(int);
        int len = *((int*)bytes);  bytes += sizeof(int);

        [windowController setScrollbarPosition:pos length:len
                                    identifier:ident];
    } else if (SetScrollbarThumbMsgID == msgid) {
        const void *bytes = [data bytes];
        long ident = *((long*)bytes);  bytes += sizeof(long);
        float val = *((float*)bytes);  bytes += sizeof(float);
        float prop = *((float*)bytes);  bytes += sizeof(float);

        [windowController setScrollbarThumbValue:val proportion:prop
                                      identifier:ident];
    } else if (SetFontMsgID == msgid) {
        const void *bytes = [data bytes];
        float size = *((float*)bytes);  bytes += sizeof(float);
        int len = *((int*)bytes);  bytes += sizeof(int);
        NSString *name = [[NSString alloc]
                initWithBytes:(void*)bytes length:len
                     encoding:NSUTF8StringEncoding];
        NSFont *font = [NSFont fontWithName:name size:size];
        if (!font) {
            // This should only happen if the default font was not loaded in
            // which case we fall back on using the Cocoa default fixed width
            // font.
            font = [NSFont userFixedPitchFontOfSize:size];
        }

        [windowController setFont:font];
        [name release];
    } else if (SetWideFontMsgID == msgid) {
        const void *bytes = [data bytes];
        float size = *((float*)bytes);  bytes += sizeof(float);
        int len = *((int*)bytes);  bytes += sizeof(int);
        if (len > 0) {
            NSString *name = [[NSString alloc]
                    initWithBytes:(void*)bytes length:len
                         encoding:NSUTF8StringEncoding];
            NSFont *font = [NSFont fontWithName:name size:size];
            [windowController setWideFont:font];

            [name release];
        } else {
            [windowController setWideFont:nil];
        }
    } else if (SetDefaultColorsMsgID == msgid) {
        const void *bytes = [data bytes];
        unsigned bg = *((unsigned*)bytes);  bytes += sizeof(unsigned);
        unsigned fg = *((unsigned*)bytes);  bytes += sizeof(unsigned);
        NSColor *back = [NSColor colorWithArgbInt:bg];
        NSColor *fore = [NSColor colorWithRgbInt:fg];

        [windowController setDefaultColorsBackground:back foreground:fore];
    } else if (ExecuteActionMsgID == msgid) {
        const void *bytes = [data bytes];
        int len = *((int*)bytes);  bytes += sizeof(int);
        NSString *actionName = [[NSString alloc]
                initWithBytes:(void*)bytes length:len
                     encoding:NSUTF8StringEncoding];

        SEL sel = NSSelectorFromString(actionName);
        [NSApp sendAction:sel to:nil from:self];

        [actionName release];
    } else if (ShowPopupMenuMsgID == msgid) {
        NSDictionary *attrs = [NSDictionary dictionaryWithData:data];

        // The popup menu enters a modal loop so delay this call so that we
        // don't block inside processCommandQueue:.
        [self performSelectorOnMainThread:@selector(popupMenuWithAttributes:)
                             withObject:attrs
			  waitUntilDone:NO
			          modes:[NSArray arrayWithObject:
					 NSDefaultRunLoopMode]];
    } else if (SetMouseShapeMsgID == msgid) {
        const void *bytes = [data bytes];
        int shape = *((int*)bytes);  bytes += sizeof(int);

        [windowController setMouseShape:shape];
    } else if (AdjustLinespaceMsgID == msgid) {
        const void *bytes = [data bytes];
        int linespace = *((int*)bytes);  bytes += sizeof(int);

        [windowController adjustLinespace:linespace];
    } else if (ActivateMsgID == msgid) {
        //NSLog(@"ActivateMsgID");
        [NSApp activateIgnoringOtherApps:YES];
        [[windowController window] makeKeyAndOrderFront:self];
    } else if (SetServerNameMsgID == msgid) {
        NSString *name = [[NSString alloc] initWithData:data
                                               encoding:NSUTF8StringEncoding];
        [self setServerName:name];
        [name release];
    } else if (EnterFullscreenMsgID == msgid) {
        const void *bytes = [data bytes];
        int fuoptions = *((int*)bytes); bytes += sizeof(int);
        int bg = *((int*)bytes);
        NSColor *back = [NSColor colorWithArgbInt:bg];

        [windowController enterFullscreen:fuoptions backgroundColor:back];
    } else if (LeaveFullscreenMsgID == msgid) {
        [windowController leaveFullscreen];
    } else if (BuffersNotModifiedMsgID == msgid) {
        [windowController setBuffersModified:NO];
    } else if (BuffersModifiedMsgID == msgid) {
        [windowController setBuffersModified:YES];
    } else if (SetPreEditPositionMsgID == msgid) {
        const int *dim = (const int*)[data bytes];
        [[[windowController vimView] textView] setPreEditRow:dim[0]
                                                      column:dim[1]];
    } else if (EnableAntialiasMsgID == msgid) {
        [[[windowController vimView] textView] setAntialias:YES];
    } else if (DisableAntialiasMsgID == msgid) {
        [[[windowController vimView] textView] setAntialias:NO];
    } else if (SetVimStateMsgID == msgid) {
        NSDictionary *dict = [NSDictionary dictionaryWithData:data];
        if (dict) {
            [vimState release];
            vimState = [dict retain];
        }
    } else if (CloseWindowMsgID == msgid) {
        [self scheduleClose];
    } else if (SetFullscreenColorMsgID == msgid) {
        const int *bg = (const int*)[data bytes];
        NSColor *color = [NSColor colorWithRgbInt:*bg];

        [windowController setFullscreenBackgroundColor:color];
    } else if (ShowFindReplaceDialogMsgID == msgid) {
        NSDictionary *dict = [NSDictionary dictionaryWithData:data];
        if (dict) {
            [[MMFindReplaceController sharedInstance]
                showWithText:[dict objectForKey:@"text"]
                       flags:[[dict objectForKey:@"flags"] intValue]];
        }
    // IMPORTANT: When adding a new message, make sure to update
    // isUnsafeMessage() if necessary!
    } else {
        NSLog(@"WARNING: Unknown message received (msgid=%d)", msgid);
    }
}

- (void)savePanelDidEnd:(NSSavePanel *)panel code:(int)code
                context:(void *)context
{
    NSString *path = (code == NSOKButton) ? [panel filename] : nil;

    // NOTE! setDialogReturn: is a synchronous call so set a proper timeout to
    // avoid waiting forever for it to finish.  We make this a synchronous call
    // so that we can be fairly certain that Vim doesn't think the dialog box
    // is still showing when MacVim has in fact already dismissed it.
    NSConnection *conn = [backendProxy connectionForProxy];
    NSTimeInterval oldTimeout = [conn requestTimeout];
    [conn setRequestTimeout:MMSetDialogReturnTimeout];

    @try {
        [backendProxy setDialogReturn:path];

        // Add file to the "Recent Files" menu (this ensures that files that
        // are opened/saved from a :browse command are added to this menu).
        if (path)
            [[NSDocumentController sharedDocumentController]
                    noteNewRecentFilePath:path];
    }
    @catch (NSException *e) {
        NSLog(@"Exception caught in %s %@", _cmd, e);
    }
    @finally {
        [conn setRequestTimeout:oldTimeout];
    }
}

- (void)alertDidEnd:(MMAlert *)alert code:(int)code context:(void *)context
{
    NSArray *ret = nil;

    code = code - NSAlertFirstButtonReturn + 1;

    if ([alert isKindOfClass:[MMAlert class]] && [alert textField]) {
        ret = [NSArray arrayWithObjects:[NSNumber numberWithInt:code],
            [[alert textField] stringValue], nil];
    } else {
        ret = [NSArray arrayWithObject:[NSNumber numberWithInt:code]];
    }

    @try {
        [backendProxy setDialogReturn:ret];
    }
    @catch (NSException *e) {
        NSLog(@"Exception caught in %s %@", _cmd, e);
    }
}

- (NSMenuItem *)menuItemForDescriptor:(NSArray *)desc
{
    if (!(desc && [desc count] > 0)) return nil;

    NSString *rootName = [desc objectAtIndex:0];
    NSArray *rootItems = [rootName hasPrefix:@"PopUp"] ? popupMenuItems
                                                       : [mainMenu itemArray];

    NSMenuItem *item = nil;
    int i, count = [rootItems count];
    for (i = 0; i < count; ++i) {
        item = [rootItems objectAtIndex:i];
        if ([[item title] isEqual:rootName])
            break;
    }

    if (i == count) return nil;

    count = [desc count];
    for (i = 1; i < count; ++i) {
        item = [[item submenu] itemWithTitle:[desc objectAtIndex:i]];
        if (!item) return nil;
    }

    return item;
}

- (NSMenu *)parentMenuForDescriptor:(NSArray *)desc
{
    if (!(desc && [desc count] > 0)) return nil;

    NSString *rootName = [desc objectAtIndex:0];
    NSArray *rootItems = [rootName hasPrefix:@"PopUp"] ? popupMenuItems
                                                       : [mainMenu itemArray];

    NSMenu *menu = nil;
    int i, count = [rootItems count];
    for (i = 0; i < count; ++i) {
        NSMenuItem *item = [rootItems objectAtIndex:i];
        if ([[item title] isEqual:rootName]) {
            menu = [item submenu];
            break;
        }
    }

    if (!menu) return nil;

    count = [desc count] - 1;
    for (i = 1; i < count; ++i) {
        NSMenuItem *item = [menu itemWithTitle:[desc objectAtIndex:i]];
        menu = [item submenu];
        if (!menu) return nil;
    }

    return menu;
}

- (NSMenu *)topLevelMenuForTitle:(NSString *)title
{
    // Search only the top-level menus.

    unsigned i, count = [popupMenuItems count];
    for (i = 0; i < count; ++i) {
        NSMenuItem *item = [popupMenuItems objectAtIndex:i];
        if ([title isEqual:[item title]])
            return [item submenu];
    }

    count = [mainMenu numberOfItems];
    for (i = 0; i < count; ++i) {
        NSMenuItem *item = [mainMenu itemAtIndex:i];
        if ([title isEqual:[item title]])
            return [item submenu];
    }

    return nil;
}

- (void)addMenuWithDescriptor:(NSArray *)desc atIndex:(int)idx
{
    if (!(desc && [desc count] > 0 && idx >= 0)) return;

    NSString *rootName = [desc objectAtIndex:0];
    if ([rootName isEqual:@"ToolBar"]) {
        // The toolbar only has one menu, we take this as a hint to create a
        // toolbar, then we return.
        if (!toolbar) {
            // NOTE! Each toolbar must have a unique identifier, else each
            // window will have the same toolbar.
            NSString *ident = [NSString stringWithFormat:@"%d", (int)self];
            toolbar = [[NSToolbar alloc] initWithIdentifier:ident];

            [toolbar setShowsBaselineSeparator:NO];
            [toolbar setDelegate:self];
            [toolbar setDisplayMode:NSToolbarDisplayModeIconOnly];
            [toolbar setSizeMode:NSToolbarSizeModeSmall];

            [windowController setToolbar:toolbar];
        }

        return;
    }

    // This is either a main menu item or a popup menu item.
    NSString *title = [desc lastObject];
    NSMenuItem *item = [[NSMenuItem alloc] init];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];

    [item setTitle:title];
    [item setSubmenu:menu];

    NSMenu *parent = [self parentMenuForDescriptor:desc];
    if (!parent && [rootName hasPrefix:@"PopUp"]) {
        if ([popupMenuItems count] <= idx) {
            [popupMenuItems addObject:item];
        } else {
            [popupMenuItems insertObject:item atIndex:idx];
        }
    } else {
        // If descriptor has no parent and its not a popup (or toolbar) menu,
        // then it must belong to main menu.
        if (!parent) parent = mainMenu;

        if ([parent numberOfItems] <= idx) {
            [parent addItem:item];
        } else {
            [parent insertItem:item atIndex:idx];
        }
    }

    [item release];
    [menu release];
}

- (void)addMenuItemWithDescriptor:(NSArray *)desc
                          atIndex:(int)idx
                              tip:(NSString *)tip
                             icon:(NSString *)icon
                    keyEquivalent:(NSString *)keyEquivalent
                     modifierMask:(int)modifierMask
                           action:(NSString *)action
                      isAlternate:(BOOL)isAlternate
{
    if (!(desc && [desc count] > 1 && idx >= 0)) return;

    NSString *title = [desc lastObject];
    NSString *rootName = [desc objectAtIndex:0];

    if ([rootName isEqual:@"ToolBar"]) {
        if (toolbar && [desc count] == 2)
            [self addToolbarItemWithLabel:title tip:tip icon:icon atIndex:idx];
        return;
    }

    NSMenu *parent = [self parentMenuForDescriptor:desc];
    if (!parent) {
        NSLog(@"WARNING: Menu item '%@' has no parent",
                [desc componentsJoinedByString:@"->"]);
        return;
    }

    NSMenuItem *item = nil;
    if (0 == [title length]
            || ([title hasPrefix:@"-"] && [title hasSuffix:@"-"])) {
        item = [NSMenuItem separatorItem];
        [item setTitle:title];
    } else {
        item = [[[NSMenuItem alloc] init] autorelease];
        [item setTitle:title];

        // Note: It is possible to set the action to a message that "doesn't
        // exist" without problems.  We take advantage of this when adding
        // "dummy items" e.g. when dealing with the "Recent Files" menu (in
        // which case a recentFilesDummy: action is set, although it is never
        // used).
        if ([action length] > 0)
            [item setAction:NSSelectorFromString(action)];
        else
            [item setAction:@selector(vimMenuItemAction:)];
        if ([tip length] > 0) [item setToolTip:tip];
        if ([keyEquivalent length] > 0) {
            [item setKeyEquivalent:keyEquivalent];
            [item setKeyEquivalentModifierMask:modifierMask];
        }
        [item setAlternate:isAlternate];

        // The tag is used to indicate whether Vim thinks a menu item should be
        // enabled or disabled.  By default Vim thinks menu items are enabled.
        [item setTag:1];
    }

    if ([parent numberOfItems] <= idx) {
        [parent addItem:item];
    } else {
        [parent insertItem:item atIndex:idx];
    }
}

- (void)removeMenuItemWithDescriptor:(NSArray *)desc
{
    if (!(desc && [desc count] > 0)) return;

    NSString *title = [desc lastObject];
    NSString *rootName = [desc objectAtIndex:0];
    if ([rootName isEqual:@"ToolBar"]) {
        if (toolbar) {
            // Only remove toolbar items, never actually remove the toolbar
            // itself or strange things may happen.
            if ([desc count] == 2) {
                int idx = [toolbar indexOfItemWithItemIdentifier:title];
                if (idx != NSNotFound)
                    [toolbar removeItemAtIndex:idx];
            }
        }
        return;
    }

    NSMenuItem *item = [self menuItemForDescriptor:desc];
    if (!item) {
        NSLog(@"Failed to remove menu item, descriptor not found: %@",
                [desc componentsJoinedByString:@"->"]);
        return;
    }

    [item retain];

    if ([item menu] == [NSApp mainMenu] || ![item menu]) {
        // NOTE: To be on the safe side we try to remove the item from
        // both arrays (it is ok to call removeObject: even if an array
        // does not contain the object to remove).
        [popupMenuItems removeObject:item];
    }

    if ([item menu])
        [[item menu] removeItem:item];

    [item release];
}

- (void)enableMenuItemWithDescriptor:(NSArray *)desc state:(BOOL)on
{
    if (!(desc && [desc count] > 0)) return;

    /*NSLog(@"%sable item %@", on ? "En" : "Dis",
            [desc componentsJoinedByString:@"->"]);*/

    NSString *rootName = [desc objectAtIndex:0];
    if ([rootName isEqual:@"ToolBar"]) {
        if (toolbar && [desc count] == 2) {
            NSString *title = [desc lastObject];
            [[toolbar itemWithItemIdentifier:title] setEnabled:on];
        }
    } else {
        // Use tag to set whether item is enabled or disabled instead of
        // calling setEnabled:.  This way the menus can autoenable themselves
        // but at the same time Vim can set if a menu is enabled whenever it
        // wants to.
        [[self menuItemForDescriptor:desc] setTag:on];
    }
}

- (void)addToolbarItemToDictionaryWithLabel:(NSString *)title
                                    toolTip:(NSString *)tip
                                       icon:(NSString *)icon
{
    // If the item corresponds to a separator then do nothing, since it is
    // already defined by Cocoa.
    if (!title || [title isEqual:NSToolbarSeparatorItemIdentifier]
               || [title isEqual:NSToolbarSpaceItemIdentifier]
               || [title isEqual:NSToolbarFlexibleSpaceItemIdentifier])
        return;

    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:title];
    [item setLabel:title];
    [item setToolTip:tip];
    [item setAction:@selector(vimToolbarItemAction:)];
    [item setAutovalidates:NO];

    NSImage *img = [NSImage imageNamed:icon];
    if (!img) {
        img = [[[NSImage alloc] initByReferencingFile:icon] autorelease];
        if (!(img && [img isValid]))
            img = nil;
    }
    if (!img) {
        NSLog(@"WARNING: Could not find image with name '%@' to use as toolbar"
               " image for identifier '%@';"
               " using default toolbar icon '%@' instead.",
               icon, title, MMDefaultToolbarImageName);

        img = [NSImage imageNamed:MMDefaultToolbarImageName];
    }

    [item setImage:img];

    [toolbarItemDict setObject:item forKey:title];

    [item release];
}

- (void)addToolbarItemWithLabel:(NSString *)label
                            tip:(NSString *)tip
                           icon:(NSString *)icon
                        atIndex:(int)idx
{
    if (!toolbar) return;

    // Check for separator items.
    if (!label) {
        label = NSToolbarSeparatorItemIdentifier;
    } else if ([label length] >= 2 && [label hasPrefix:@"-"]
                                   && [label hasSuffix:@"-"]) {
        // The label begins and ends with '-'; decided which kind of separator
        // item it is by looking at the prefix.
        if ([label hasPrefix:@"-space"]) {
            label = NSToolbarSpaceItemIdentifier;
        } else if ([label hasPrefix:@"-flexspace"]) {
            label = NSToolbarFlexibleSpaceItemIdentifier;
        } else {
            label = NSToolbarSeparatorItemIdentifier;
        }
    }

    [self addToolbarItemToDictionaryWithLabel:label toolTip:tip icon:icon];

    int maxIdx = [[toolbar items] count];
    if (maxIdx < idx) idx = maxIdx;

    [toolbar insertItemWithItemIdentifier:label atIndex:idx];
}

- (void)popupMenuWithDescriptor:(NSArray *)desc
                          atRow:(NSNumber *)row
                         column:(NSNumber *)col
{
    NSMenu *menu = [[self menuItemForDescriptor:desc] submenu];
    if (!menu) return;

    id textView = [[windowController vimView] textView];
    NSPoint pt;
    if (row && col) {
        // TODO: Let textView convert (row,col) to NSPoint.
        int r = [row intValue];
        int c = [col intValue];
        NSSize cellSize = [textView cellSize];
        pt = NSMakePoint((c+1)*cellSize.width, (r+1)*cellSize.height);
        pt = [textView convertPoint:pt toView:nil];
    } else {
        pt = [[windowController window] mouseLocationOutsideOfEventStream];
    }

    NSEvent *event = [NSEvent mouseEventWithType:NSRightMouseDown
                           location:pt
                      modifierFlags:0
                          timestamp:0
                       windowNumber:[[windowController window] windowNumber]
                            context:nil
                        eventNumber:0
                         clickCount:0
                           pressure:1.0];

    [NSMenu popUpContextMenu:menu withEvent:event forView:textView];
}

- (void)popupMenuWithAttributes:(NSDictionary *)attrs
{
    if (!attrs) return;

    [self popupMenuWithDescriptor:[attrs objectForKey:@"descriptor"]
                            atRow:[attrs objectForKey:@"row"]
                           column:[attrs objectForKey:@"column"]];
}

- (void)connectionDidDie:(NSNotification *)notification
{
    //NSLog(@"%@ %s%@", [self className], _cmd, notification);
    [self scheduleClose];
}

- (void)scheduleClose
{
    // NOTE!  This message can arrive at pretty much anytime, e.g. while
    // the run loop is the 'event tracking' mode.  This means that Cocoa may
    // well be in the middle of processing some message while this message is
    // received.  If we were to remove the vim controller straight away we may
    // free objects that Cocoa is currently using (e.g. view objects).  The
    // following call ensures that the vim controller is not released until the
    // run loop is back in the 'default' mode.
    [[MMAppController sharedInstance]
            performSelectorOnMainThread:@selector(removeVimController:)
                             withObject:self
			  waitUntilDone:NO
			          modes:[NSArray arrayWithObject:
					 NSDefaultRunLoopMode]];
}

@end // MMVimController (Private)




@implementation MMAlert
- (void)dealloc
{
    [textField release];  textField = nil;
    [super dealloc];
}

- (void)setTextFieldString:(NSString *)textFieldString
{
    [textField release];
    textField = [[NSTextField alloc] init];
    [textField setStringValue:textFieldString];
}

- (NSTextField *)textField
{
    return textField;
}

- (void)setInformativeText:(NSString *)text
{
    if (textField) {
        // HACK! Add some space for the text field.
        [super setInformativeText:[text stringByAppendingString:@"\n\n\n"]];
    } else {
        [super setInformativeText:text];
    }
}

- (void)beginSheetModalForWindow:(NSWindow *)window
                   modalDelegate:(id)delegate
                  didEndSelector:(SEL)didEndSelector
                     contextInfo:(void *)contextInfo
{
    [super beginSheetModalForWindow:window
                      modalDelegate:delegate
                     didEndSelector:didEndSelector
                        contextInfo:contextInfo];

    // HACK! Place the input text field at the bottom of the informative text
    // (which has been made a bit larger by adding newline characters).
    NSView *contentView = [[self window] contentView];
    NSRect rect = [contentView frame];
    rect.origin.y = rect.size.height;

    NSArray *subviews = [contentView subviews];
    unsigned i, count = [subviews count];
    for (i = 0; i < count; ++i) {
        NSView *view = [subviews objectAtIndex:i];
        if ([view isKindOfClass:[NSTextField class]]
                && [view frame].origin.y < rect.origin.y) {
            // NOTE: The informative text field is the lowest NSTextField in
            // the alert dialog.
            rect = [view frame];
        }
    }

    rect.size.height = MMAlertTextFieldHeight;
    [textField setFrame:rect];
    [contentView addSubview:textField];
    [textField becomeFirstResponder];
}

@end // MMAlert




    static BOOL
isUnsafeMessage(int msgid)
{
    // Messages that may release Cocoa objects must be added to this list.  For
    // example, UpdateTabBarMsgID may delete NSTabViewItem objects so it goes
    // on this list.
    static int unsafeMessages[] = { // REASON MESSAGE IS ON THIS LIST:
        //OpenWindowMsgID,            // Changes lots of state
        UpdateTabBarMsgID,          // May delete NSTabViewItem
        RemoveMenuItemMsgID,        // Deletes NSMenuItem
        DestroyScrollbarMsgID,      // Deletes NSScroller
        ExecuteActionMsgID,         // Impossible to predict
        ShowPopupMenuMsgID,         // Enters modal loop
        ActivateMsgID,              // ?
        EnterFullscreenMsgID,       // Modifies delegate of window controller
        LeaveFullscreenMsgID,       // Modifies delegate of window controller
        CloseWindowMsgID,           // See note below
    };

    // NOTE about CloseWindowMsgID: If this arrives at the same time as say
    // ExecuteActionMsgID, then the "execute" message will be lost due to it
    // being queued and handled after the "close" message has caused the
    // controller to cleanup...UNLESS we add CloseWindowMsgID to the list of
    // unsafe messages.  This is the _only_ reason it is on this list (since
    // all that happens in response to it is that we schedule another message
    // for later handling).

    int i, count = sizeof(unsafeMessages)/sizeof(unsafeMessages[0]);
    for (i = 0; i < count; ++i)
        if (msgid == unsafeMessages[i])
            return YES;

    return NO;
}
