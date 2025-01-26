Idea: would be cool to not need an intermediate source file for game engines. Some engines use xml, or yaml, or whatever to represent assets, 
which then get "Compiled" for actual use in the runtime/shipping client. It would be a reasonable speedup to not need to parse those intermediate file formats, 
and instead just have the binary files themselves be the authoring AND shipping file format.

The main argument against this is source control. You can't do arbitrary merges/diffs of opaque binary file formats. 
If this was solved, I can't really think of any other big downsides to just using the binary file formats, and would speed up/simplify engine loading a fair bit.

To facilitate this idea, I want to make a tool that can merge binary file formats for version control systems. 
To do that, we need some info about the file format. This info is typically already represented in code through structure definitions. 
Those structure definitions are available through debugging information emitted by compilers. 
The idea here is to take in data about the structure of a file format from the code itself (example lib that uses the same idea: https://github.com/jam1garner/binrw) 
and use that data to properly do merge/diffs of arbitrary binary file formats given their internal structure.

Work todo:

BOOKMARK: 
proper thought path: we are merging entire File format layouts. 
Before i was thinking of merging multiple copies of the same structure layout. THis isn't the case.
The base might have 3 fields, the local might have removed one, and the remote might have added and changed one.
All of these changes should be properly reconciled.

merge/diff hardcoded binary file format
- merging a single field modification
- merging a single field addition
- merging a single field removal
- merging field reordering
- merging combinations of the above
hardcoded format -> merge/diff based on some sort of "file format spec"
derive the "file format spec" from program debug info

another possible (much simpler) approach would be to have the merge granularity be at a "struct" level, rather than field level
so two changes to the *same depth* struct cannot be merged.
but if you had
struct Data
{
    int y;
    struct SubData
    {
        int x;
    }
    SubData subdata;
}
Someone can make changes to "y" and to data inside "subdata" independently without affecting each other. Merges would always be correct with this approach.



perforce integration:
%1 source (theirs)
%2 destination (yours)
%b base
%r result


Resources:

"Bill Ritcher's excellent paper "A Trustworthy 3-Way Merge" talks about some of the common gotchas with three way merging and clever solutions to them that commercial SCM packages have used."
https://www.guiffy.com/SureMergeWP.html


There's a formal analysis of the diff3 algorithm, with pseudocode, in this paper: http://www.cis.upenn.edu/~bcpierce/papers/diff3-short.pdf
It is titled "A Formal Investigation of Diff3" and written by Sanjeev Khanna, Keshav Kunal, and Benjamin C. Pierce from Yahoo.


https://github.com/jmacd/xdelta/tree/release3_1_apl/xdelta3
https://meta-diff.sourceforge.net/
https://github.com/jam1garner/binrw // example of a good simple user-facing api. Just define your struct and that's it.
https://homes.cs.washington.edu/~mernst/pubs/merge-evaluation-ase2024.pdf
https://github.com/GumTreeDiff/gumtree


