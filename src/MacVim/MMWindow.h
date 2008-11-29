/* vi:set ts=8 sts=4 sw=4 ft=objc:
 *
 * VIM - Vi IMproved		by Bram Moolenaar
 *				MacVim GUI port by Bjorn Winckler
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

#import <Cocoa/Cocoa.h>



@interface MMWindow : NSWindow {
    NSBox       *tablineSeparator;
    NSRect      userFrame;
}

- (id)initWithContentRect:(NSRect)rect
                styleMask:(unsigned int)style
                  backing:(NSBackingStoreType)bufferingType
                    defer:(BOOL)flag;

- (BOOL)hideTablineSeparator:(BOOL)hide;

- (NSRect)contentRectForFrameRect:(NSRect)frame;
- (NSRect)frameRectForContentRect:(NSRect)rect;
- (void)setContentMinSize:(NSSize)size;
- (void)setContentMaxSize:(NSSize)size;
- (void)setContentSize:(NSSize)size;

@end
